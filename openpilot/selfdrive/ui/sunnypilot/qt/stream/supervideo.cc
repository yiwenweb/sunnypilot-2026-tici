#include "selfdrive/ui/sunnypilot/qt/stream/supervideo.h"

#include <unistd.h>

#include <chrono>
#include <cstring>

#include <algorithm>

#include <QImage>

#include "common/swaglog.h"
#include "common/timing.h"
#include "common/util.h"

namespace {
  constexpr int STREAM_PORT = 8082;
  constexpr int STREAM_FPS = 24;         // UI 线程只承担 grab；24fps 给事件循环留足余量
  constexpr int STREAM_WIDTH = 1280;     // VENUS 要求宽高 128 对齐：1280/640 均满足
  constexpr int STREAM_HEIGHT = 640;
  constexpr int NV12_SIZE = STREAM_WIDTH * STREAM_HEIGHT * 3 / 2;
  constexpr int MAX_IN_FLIGHT = 6;       // 输入缓冲 9，留余量
  const char *VENC_DEVICE = "/dev/v4l/by-path/platform-aa00000.qcom_vidc-video-index1";
}

SuperVideoStreamer::SuperVideoStreamer(QWidget *source_widget, QObject *parent)
    : QObject(parent), src(source_widget) {
  for (auto &b : nv12_bufs) b = VisionBuf{};
  for (auto &busy : nv12_busy) busy = false;

  server = new QTcpServer(this);
  connect(server, &QTcpServer::newConnection, this, &SuperVideoStreamer::onNewConnection);

  param_timer = new QTimer(this);
  param_timer->setSingleShot(false);
  param_timer->start(1000);              // 1Hz 参数轮询，改开关立即生效
  connect(param_timer, &QTimer::timeout, this, &SuperVideoStreamer::pollParam);

  frame_timer = new QTimer(this);
  frame_timer->setTimerType(Qt::CoarseTimer);
  frame_timer->setInterval(1000 / STREAM_FPS);
  connect(frame_timer, &QTimer::timeout, this, &SuperVideoStreamer::captureFrame);

  pollParam();
}

SuperVideoStreamer::~SuperVideoStreamer() {
  stopStreaming();
  if (worker_started) {
    quit_worker = true;
    want_run = false;
    frame_cv.notify_all();
    if (worker.joinable()) worker.join();
    // worker 已退出，缓冲只有 worker 用过，安全释放
    if (nv12_allocated) {
      for (auto &b : nv12_bufs) b.free();
      nv12_allocated = false;
    }
  }
}

void SuperVideoStreamer::pollParam() {
  bool want = params.getBool("SuperVideoStream");
  if (want == enabled) return;
  enabled = want;
  if (enabled) {
    startStreaming();
  } else {
    stopStreaming();
  }
}

void SuperVideoStreamer::startStreaming() {
  if (streaming) return;
  if (access(VENC_DEVICE, F_OK) != 0) {
    LOGE("supervideo: hardware encoder device not found, streaming disabled");
    enabled = false;
    return;
  }
  if (!server->listen(QHostAddress::Any, STREAM_PORT)) {
    LOGE("supervideo: failed to listen on port %d", STREAM_PORT);
    enabled = false;
    return;
  }
  streaming = true;
  in_flight = 0;
  want_run = true;                       // 编码器由 worker 线程异步打开（阻塞 ioctl 不碰 UI）
  if (!worker_started) {
    quit_worker = false;
    worker = std::thread(&SuperVideoStreamer::workerLoop, this);
    worker_started = true;
  }
  frame_cv.notify_all();
  frame_timer->start();
  LOGW("supervideo: streaming started (%dx%d @ %d fps, tcp:%d)", STREAM_WIDTH, STREAM_HEIGHT, STREAM_FPS, STREAM_PORT);
}

void SuperVideoStreamer::stopStreaming() {
  if (!streaming) return;
  frame_timer->stop();
  want_run = false;                      // worker 自己关编码器（close 里的 ioctl 可能阻塞，不上 UI 线程）
  frame_cv.notify_all();
  for (auto *c : clients) {
    c->disconnectFromHost();
  }
  clients.clear();
  server->close();
  std::lock_guard<std::mutex> lock(pkt_mutex);
  pending_data.clear();
  streaming = false;
  LOGW("supervideo: streaming stopped");
}

void SuperVideoStreamer::onEncoderFailed() {
  LOGE("supervideo: encoder open failed in worker thread, disabling streaming");
  stopStreaming();
  enabled = false;
}

// ---------------- worker 线程 ----------------

void SuperVideoStreamer::workerLoop() {
  while (!quit_worker) {
    const bool run = want_run;

    if (run && !encoder) {
      openEncoder();                     // 阻塞 ioctl 在 worker，UI 线程不受影响
      if (!encoder) {
        QMetaObject::invokeMethod(this, "onEncoderFailed", Qt::QueuedConnection);
        // 等状态变化再重试，避免疯狂重开 msm_vidc 会话
        std::unique_lock<std::mutex> lk(frame_mutex);
        frame_cv.wait_for(lk, std::chrono::seconds(3),
                          [&] { return quit_worker.load() || want_run.load() != run; });
        continue;
      }
      in_flight = 0;
    } else if (!run && encoder) {
      closeEncoder();
    }

    if (!encoder) {
      // 空闲：等启动/退出信号
      std::unique_lock<std::mutex> lk(frame_mutex);
      frame_cv.wait_for(lk, std::chrono::milliseconds(300),
                        [&] { return quit_worker.load() || (want_run.load() && !encoder); });
      continue;
    }

    QImage img;
    {
      std::unique_lock<std::mutex> lk(frame_mutex);
      if (frame_q.empty()) {
        frame_cv.wait_for(lk, std::chrono::milliseconds(200),
                          [&] { return quit_worker.load() || !frame_q.empty() || !want_run.load(); });
        continue;                        // 回循环头处理 run/quit 状态
      }
      img = std::move(frame_q.front());
      frame_q.pop_front();
    }

    // 缩放 + RGB32 统一（QImage 隐式共享，worker 独占此副本）
    QImage scaled = img.scaled(STREAM_WIDTH, STREAM_HEIGHT, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (scaled.isNull()) continue;
    if (scaled.format() != QImage::Format_ARGB32 && scaled.format() != QImage::Format_RGB32) {
      scaled = scaled.convertToFormat(QImage::Format_RGB32);
    }

    if (in_flight >= MAX_IN_FLIGHT) continue;  // 硬件积压，丢帧

    // 从池里挑一个硬件已归还的 NV12 缓冲（USERPTR 交给 DMA 后未归前绝不能覆写）
    int slot = -1;
    for (int i = 0; i < BUF_IN_COUNT; ++i) {
      bool expected = false;   // compare_exchange 需要左值 expected
      if (nv12_busy[i].compare_exchange_strong(expected, true)) { slot = i; break; }
    }
    if (slot < 0) continue;                    // 理论不发生（MAX_IN_FLIGHT < BUF_IN_COUNT），防御

    VisionBuf &vb = nv12_bufs[slot];
    rgbToNv12(scaled, (uint8_t *)vb.addr, STREAM_WIDTH, STREAM_HEIGHT);
    vb.width = STREAM_WIDTH;
    vb.height = STREAM_HEIGHT;
    vb.stride = STREAM_WIDTH;

    int64_t ts_eof = nanos_since_boot();
    VisionIpcBufExtra extra = {};
    extra.timestamp_eof = ts_eof;
    in_flight++;
    {
      std::lock_guard<std::mutex> lk(enc_mutex);
      if (encoder) {
        encoder->encode_frame(&vb, &extra);
      } else {
        nv12_busy[slot] = false;               // 竞态兜底：encoder 正在被关闭
        in_flight--;
      }
    }
  }

  // 退出：编码器必须在 worker 自己关（close 里的 ioctl 不上 UI 线程）
  std::lock_guard<std::mutex> lk(enc_mutex);
  closeEncoder();
}

void SuperVideoStreamer::openEncoder() {
  try {
    EncoderInfo info = {};
    // 关键：publish_name 必须是 cereal 服务表里已有的名字。
    // V4LEncoder 的基类构造里会执行 new PubMaster({publish_name})，而 PubMaster
    // 内部第一句就是 assert(services.count(name) > 0) —— 传一个自造名字会直接
    // SIGABRT 杀掉 UI 进程（assert 不可 catch），openpilot 随即无限重启 UI，
    // 表现就是"一开开关整机卡死"。这里复用合法的 livestream 服务名；本类始终提供
    // packet_callback，永远不会走 publisher_publish 路径，因此不会真的向该服务发布。
    info.publish_name = "livestreamNarrowRoadEncodeData";
    info.filename = "supervideo.h264";
    info.record = false;
    info.is_live = true;
    info.frame_width = STREAM_WIDTH;
    info.frame_height = STREAM_HEIGHT;
    info.fps = STREAM_FPS;
    info.get_settings = [](int) { return EncoderSettings::StreamEncoderSettings(); };
    // 与 publish_name 保持一致（这三个函数仅在 publisher_publish 路径使用）
    info.get_encode_data_func = &cereal::Event::Reader::getLivestreamNarrowRoadEncodeData;
    info.set_encode_idx_func = &cereal::Event::Builder::setLivestreamNarrowRoadEncodeIdx;
    info.init_encode_data_func = &cereal::Event::Builder::initLivestreamNarrowRoadEncodeData;

    encoder = new V4LEncoder(info, STREAM_WIDTH, STREAM_HEIGHT,
                             {.packet_callback = [this](uint8_t *d, size_t s, int64_t ts, bool config, bool key) {
                                packetHandler(d, s, ts, config, key);
                              },
                              .input_format = V4L2_PIX_FMT_NV12,
                              .input_done_callback = [this](VisionBuf *b) {
                                // dequeue 线程：硬件已归还该输入缓冲，标记可复用
                                for (int i = 0; i < BUF_IN_COUNT; ++i) {
                                  if (&nv12_bufs[i] == b) { nv12_busy[i] = false; break; }
                                }
                                in_flight--;
                              },
                              .max_performance = true});
    encoder->encoder_open();
    encoder_ready = true;

    if (!nv12_allocated) {
      for (auto &b : nv12_bufs) b.allocate(NV12_SIZE);
      nv12_allocated = true;
    }
  } catch (const std::exception &e) {
    LOGE("supervideo: encoder init failed: %s", e.what());
    closeEncoder();
  }
}

void SuperVideoStreamer::closeEncoder() {
  encoder_ready = false;
  if (encoder) {
    encoder->encoder_close();
    delete encoder;
    encoder = nullptr;
  }
}

// ---------------- UI 线程抓帧 ----------------

void SuperVideoStreamer::captureFrame() {
  if (!streaming || !src) return;

  QElapsedTimer t;
  t.start();

  // UI 线程只做这一件事：抓窗口 → 转 QImage → 入队。其余全部在 worker。
  QPixmap pm = src->grab();
  if (pm.isNull()) return;
  QImage img = pm.toImage();
  if (img.isNull()) return;

  // UI 线程预算保护：grab 耗时 EMA 超过间隔的 60% 就自动降速（下限 ~10fps），
  // 宁可流帧率低一点，也绝不让抓帧拖死 UI 事件循环
  double ms = (double)t.elapsed();
  grab_ema_ms = (grab_ema_ms == 0) ? ms : (grab_ema_ms * 0.9 + ms * 0.1);
  int cur = frame_timer->interval();
  if (grab_ema_ms > cur * 0.6 && cur < 100) {
    frame_timer->setInterval(std::min(100, cur + 8));
    LOGW("supervideo: grab %.1fms, back off to %dms/frame", grab_ema_ms, frame_timer->interval());
  }

  {
    std::lock_guard<std::mutex> lk(frame_mutex);
    if (frame_q.size() >= MAX_QUEUED_FRAMES) return;  // worker 积压，丢帧不阻塞 UI
    frame_q.push_back(std::move(img));
  }
  frame_cv.notify_one();
}

void SuperVideoStreamer::rgbToNv12(const QImage &img, uint8_t *nv12, int width, int height) {
  uint8_t *y_plane = nv12;
  uint8_t *uv_plane = nv12 + width * height;
  const int uv_stride = width;

  for (int row = 0; row < height; ++row) {
    const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(row));
    uint8_t *y_row = y_plane + row * width;
    for (int col = 0; col < width; ++col) {
      const QRgb &p = line[col];
      y_row[col] = (uint8_t)((77 * qRed(p) + 150 * qGreen(p) + 29 * qBlue(p)) >> 8);
    }
  }
  for (int row = 0; row < height; row += 2) {
    const QRgb *l0 = reinterpret_cast<const QRgb *>(img.constScanLine(row));
    const QRgb *l1 = reinterpret_cast<const QRgb *>(img.constScanLine(row + 1));
    uint8_t *uv_row = uv_plane + (row / 2) * uv_stride;
    for (int col = 0; col < width; col += 2) {
      // 2x2 块均值
      int r = (qRed(l0[col]) + qRed(l0[col + 1]) + qRed(l1[col]) + qRed(l1[col + 1])) >> 2;
      int g = (qGreen(l0[col]) + qGreen(l0[col + 1]) + qGreen(l1[col]) + qGreen(l1[col + 1])) >> 2;
      int b = (qBlue(l0[col]) + qBlue(l0[col + 1]) + qBlue(l1[col]) + qBlue(l1[col + 1])) >> 2;
      int u = ((-43 * r - 85 * g + 128 * b) >> 8) + 128;
      int v = ((128 * r - 107 * g - 21 * b) >> 8) + 128;
      uv_row[col] = (uint8_t)std::clamp(u, 0, 255);
      uv_row[col + 1] = (uint8_t)std::clamp(v, 0, 255);
    }
  }
}

// ---------------- 编码输出 → 客户端 ----------------

// 由 V4LEncoder 的 dequeue 线程回调（非 UI 线程）
void SuperVideoStreamer::packetHandler(uint8_t *data, size_t size, int64_t ts, bool config, bool keyframe) {
  (void)ts; (void)keyframe;
  std::lock_guard<std::mutex> lock(pkt_mutex);
  if (config) {
    // SPS/PPS：缓存并即时下发（encoder_open 后第一个包）
    pending_config.assign((const char *)data, size);
    pending_data.append((const char *)data, size);
  } else {
    pending_data.append((const char *)data, size);
  }
  if (pending_data.size() > 4 * 1024 * 1024) {
    pending_data.clear();                // 无客户端时防内存无限增长
  }
  QMetaObject::invokeMethod(this, "flushClients", Qt::QueuedConnection);
}

void SuperVideoStreamer::onNewConnection() {
  while (server->hasPendingConnections()) {
    QTcpSocket *c = server->nextPendingConnection();
    if (!c) break;
    c->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    clients.push_back(c);
    connect(c, &QTcpSocket::disconnected, this, &SuperVideoStreamer::onClientDisconnected);
    LOGW("supervideo: client connected (%d total)", (int)clients.size());
    // 新客户端先补一份 SPS/PPS，避免等下一个 GOP
    {
      std::lock_guard<std::mutex> lock(pkt_mutex);
      if (!pending_config.empty()) {
        c->write(pending_config.data(), pending_config.size());
      }
    }
    // 请求关键帧：try_lock 拿不到（worker 正在开/关编码器）就跳过，绝不阻塞 UI
    if (encoder_ready) {
      std::unique_lock<std::mutex> lk(enc_mutex, std::try_to_lock);
      if (lk.owns_lock() && encoder) encoder->request_keyframe();
    }
  }
}

void SuperVideoStreamer::onClientDisconnected() {
  clients.erase(std::remove_if(clients.begin(), clients.end(),
                               [](QTcpSocket *c) { return c->state() == QAbstractSocket::UnconnectedState; }),
                clients.end());
}

void SuperVideoStreamer::flushClients() {
  std::string data;
  {
    std::lock_guard<std::mutex> lock(pkt_mutex);
    data.swap(pending_data);
  }
  if (data.empty() || clients.empty()) return;

  QByteArray chunk(data.data(), (int)data.size());
  for (auto *c : clients) {
    if (c->state() == QAbstractSocket::ConnectedState && c->bytesToWrite() < 2 * 1024 * 1024) {
      c->write(chunk);
    }
  }
}
