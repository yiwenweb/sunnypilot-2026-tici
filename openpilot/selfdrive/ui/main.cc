#include <sys/resource.h>

#include <QApplication>
#include <QTranslator>

#include "openpilot/system/hardware/hw.h"
#include "openpilot/selfdrive/ui/qt/util.h"
#include "openpilot/selfdrive/ui/qt/window.h"

#ifdef SUNNYPILOT
#include "openpilot/selfdrive/ui/sunnypilot/qt/window.h"
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
  QString qm_path = QApplication::applicationDirPath() + "/translations/" + translation_file + ".qm";
  if (!translator.load(qm_path) && translation_file.length()) {
    qCritical() << "Failed to load translation file:" << qm_path;
  }
  a.installTranslator(&translator);

  MainWindow w;
  setMainWindow(&w);
  a.installEventFilter(&w);
  return a.exec();
}
