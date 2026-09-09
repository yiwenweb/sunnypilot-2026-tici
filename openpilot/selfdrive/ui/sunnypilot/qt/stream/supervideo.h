#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QElapsedTimer>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QWidget>

#include "cereal/messaging/messaging.h"
#include "msgq/visionipc/visionbuf.h"
#include "system/loggerd/encoder/v4l_encoder.h"

// 超级视频（方案1）：UI 进程内抓帧 → 缩放 1280x640 → NV12 →
// msm_vidc 硬件编码（V4LEncoder，packet_callback 模式，不发布 cereal）→
// H.264 Annex-B 原始流，TCP :8082。触摸反向控制走 c3tools/stream_server.py 的
// /input → c3touchd_ev2 → /dev/input/event2（与本类解耦，见 c3tools/）。
//
// 协议：客户端 TCP 连上后先收到缓存的 CODECCONFIG（SPS/PPS，Annex-B），
// 之后是实时 Annex-B NALU 流。App 端按 start code 切 NALU 喂 MediaCodec。
//
// 线程模型（v2，修复开开关卡死整机的问题）：
//   - UI 线程：只做 QWidget::grab() + toImage，帧入队（≤2 帧，满了就丢）。
//     V4L2 ioctl（S_FMT/REQBUFS/STREAMON）与 msm_vidc 固件争用时可阻塞，
//     绝不允许发生在 UI 线程 —— 全部在 worker 线程执行。
//   - worker 线程：编码器生命周期 + scaled/NV12 转换 + encode_frame。
//     随 stopStreaming 挂起（want_run=false），startStreaming 唤醒复用，
//     不反复建线程，也不在 UI 线程 join（worker 可能卡在 ioctl 里）。
//   - V4LEncoder 自带的 dequeue 线程：回收输出，经 packet_callback 交
//     pkt_mutex 保护的缓冲，Queued invoke 回 UI 线程写 socket。
class SuperVideoStreamer : public QObject {
  Q_OBJECT

public:
  explicit SuperVideoStreamer(QWidget *source_widget, QObject *parent = nullptr);
  ~SuperVideoStreamer() override;

private slots:
  void pollParam();                      // 1Hz：读 SuperVideoStream 参数
  void startStreaming();
  void stopStreaming();
  void captureFrame();                   // UI 线程：仅 grab + 入队
  void onNewConnection();
  void onClientDisconnected();
  void flushClients();                   // 把编码输出攒下的码流写给客户端
  void onEncoderFailed();                // worker 打不开编码器 → UI 自动关停

private:
  void workerLoop();                     // worker 线程主循环
  void openEncoder();                    // worker only（阻塞 ioctl）
  void closeEncoder();                   // worker only
  static void rgbToNv12(const QImage &img, uint8_t *nv12, int width, int height);
  void packetHandler(uint8_t *data, size_t size, int64_t ts, bool config, bool keyframe);

  QWidget *src;
  QTimer *param_timer;
  QTimer *frame_timer;
  QTcpServer *server;
  std::vector<QTcpSocket *> clients;

  Params params;
  bool enabled = false;
  bool streaming = false;

  // UI → worker 帧队列（QImage 隐式共享，入队是浅拷贝）
  std::mutex frame_mutex;
  std::condition_variable frame_cv;
  std::deque<QImage> frame_q;
  static constexpr size_t MAX_QUEUED_FRAMES = 2;

  // worker 线程：随首次 start 启动一次，之后按 want_run 挂起/唤醒
  std::thread worker;
  bool worker_started = false;           // UI 线程访问（构造/析构/pollParam 同线程）
  std::atomic<bool> want_run{false};
  std::atomic<bool> quit_worker{false};
  std::atomic<bool> encoder_ready{false};

  // 编码器：open/close/encode 仅 worker 线程；UI 线程 request_keyframe 走
  // encoder_ready + enc_mutex.try_lock（拿不到就跳过，绝不阻塞 UI）
  std::mutex enc_mutex;
  V4LEncoder *encoder = nullptr;
  // NV12 缓冲池：USERPTR 模式 DMA 直接读这块内存，硬件归还前绝不能复用。
  // 池大小 = BUF_IN_COUNT(9)，nv12_busy 由 dequeue 线程的 input_done_callback 释放。
  VisionBuf nv12_bufs[BUF_IN_COUNT];
  std::atomic<bool> nv12_busy[BUF_IN_COUNT];
  std::atomic<int> in_flight{0};         // 已喂硬件未回收的输入缓冲数（dequeue 线程递减）
  bool nv12_allocated = false;           // worker only

  // UI 线程抓帧耗时统计（仅 UI 线程访问），用于自适应降速保护事件循环
  double grab_ema_ms = 0;

  // 编码输出 → UI 线程 的码流中转（dequeue 线程写，UI 线程读）
  std::mutex pkt_mutex;
  std::string pending_config;            // SPS/PPS，缓存给新客户端
  std::string pending_data;              // 待 flush 的 Annex-B 数据
};
