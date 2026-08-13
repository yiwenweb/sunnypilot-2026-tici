#include "openpilot/selfdrive/ui/qt/qt_window.h"

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
  wl_surface *s = reinterpret_cast<wl_surface*>(native->nativeResourceForWindow("surface", w->windowHandle()));
  if (s != nullptr) {
    wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
    wl_surface_commit(s);
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
