#include "openpilot/selfdrive/ui/qt/qt_window.h"

#include <QDebug>
#include <QThread>
#include <QApplication>

void setMainWindow(QWidget *w) {
  const float scale = util::getenv("SCALE", 1.0f);
  const QSize sz = QGuiApplication::primaryScreen()->size();

  if (Hardware::PC() && scale == 1.0 && !(sz - DEVICE_SCREEN_SIZE).isValid()) {
    w->setMinimumSize(QSize(640, 480)); // allow resize smaller than fullscreen
    w->setMaximumSize(DEVICE_SCREEN_SIZE);
    w->resize(sz);
  } else {
    w->setFixedSize(DEVICE_SCREEN_SIZE * scale);
  }
  w->show();

#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();

  // The wayland surface may not exist yet right after show(); retry briefly
  // before giving up, otherwise the buffer transform is silently skipped and
  // the UI shows up rotated wrong on the comma three portrait panel.
  wl_surface *s = nullptr;
  for (int i = 0; i < 20 && s == nullptr; ++i) {
    s = reinterpret_cast<wl_surface*>(native->nativeResourceForWindow("surface", w->windowHandle()));
    if (s == nullptr) {
      QThread::msleep(50);
      QApplication::processEvents();
    }
  }
  qWarning() << "setMainWindow: wl_surface =" << static_cast<const void*>(s);
  if (s != nullptr) {
    wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
    wl_surface_commit(s);
  } else {
    qCritical() << "setMainWindow: no wl_surface after retries - buffer transform NOT applied";
  }

  w->setWindowState(Qt::WindowFullScreen);
  w->setVisible(true);

  void *egl = native->nativeResourceForWindow("egldisplay", w->windowHandle());
  if (egl == nullptr) {
    qCritical() << "Qt UI missing egldisplay for window; continuing without assert";
  }
#endif
}


extern "C" {
  void set_main_window(void *w) {
    setMainWindow((QWidget*)w);
  }
}
