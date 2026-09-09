#include <sys/resource.h>

#include <QApplication>
#include <QTranslator>

#include "openpilot/common/hardware/hw.h"
#include "openpilot/selfdrive/ui/qt/util.h"
#include "openpilot/selfdrive/ui/qt/window.h"

#ifdef SUNNYPILOT
#include "openpilot/selfdrive/ui/sunnypilot/qt/window.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/stream/supervideo.h"
#define MainWindow MainWindowSP
#else
#include "openpilot/selfdrive/ui/qt/qt_window.h"
#endif

int main(int argc, char *argv[]) {
  setpriority(PRIO_PROCESS, 0, -20);

  qInstallMessageHandler(swagLogMessageHandler);
  initApp(argc, argv);

  QApplication a(argc, argv);

  QTranslator translator;
  QString translation_file = QString::fromStdString(Params().get("LanguageSetting"));
  // .qm files are embedded into the ui binary via translations_assets.qrc,
  // matching sp2025-gf. Load from the QRC alias path (":/<lang-code>").
  if (!translator.load(QString(":/%1").arg(translation_file)) && translation_file.length()) {
    qCritical() << "Failed to load translation file:" << translation_file;
  }
  a.installTranslator(&translator);

  MainWindow w;
  setMainWindow(&w);
  a.installEventFilter(&w);
#ifdef SUNNYPILOT
  // 超级视频：msm_vidc 硬编码 H.264 推流（SuperVideoStream 参数控制，默认关）
  SuperVideoStreamer *supervideo = new SuperVideoStreamer(&w, &w);
  Q_UNUSED(supervideo);
#endif
  return a.exec();
}
