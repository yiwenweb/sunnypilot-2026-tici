#pragma once

#include <mutex>
#include <string>
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
class SuperVideoStreamer : public QObject {
  Q_OBJECT

public:
  explicit SuperVideoStreamer(QWidget *source_widget, QObject *parent = nullptr);
  ~SuperVideoStreamer() override;

private slots:
  void pollParam();                      // 1Hz：读 SuperVideoStream 参数
  void startStreaming();
  void stopStreaming();
  void captureFrame();                   // QTimer 槽：抓帧 + 编码
  void onNewConnection();
  void onClientDisconnected();
  void flushClients();                   // 把编码线程攒下的码流写给客户端

private:
  void openEncoder();
  void closeEncoder();
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

  // 编码器
  V4LEncoder *encoder = nullptr;
  VisionBuf nv12_bufs[2];
  int cur_buf = 0;
  int in_flight = 0;                     // 已喂硬件未回收的输入缓冲数
  bool nv12_allocated = false;

  // 编码线程 → UI 线程 的码流中转
  std::mutex pkt_mutex;
  std::string pending_config;            // SPS/PPS，缓存给新客户端
  std::string pending_data;              // 待 flush 的 Annex-B 数据
};
