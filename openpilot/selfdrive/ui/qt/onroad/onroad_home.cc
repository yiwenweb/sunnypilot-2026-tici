#include "openpilot/selfdrive/ui/qt/onroad/onroad_home.h"

#include <QPainter>
#include <QStackedLayout>

#include "openpilot/selfdrive/ui/qt/util.h"

OnroadWindow::OnroadWindow(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *main_layout  = new QVBoxLayout(this);
  main_layout->setMargin(UI_BORDER_SIZE);
  QStackedLayout *stacked_layout = new QStackedLayout;
  stacked_layout->setStackingMode(QStackedLayout::StackAll);
  main_layout->addLayout(stacked_layout);

  nvg = new AnnotatedCameraWidget(VISION_STREAM_NARROW_ROAD, this);

  QWidget * split_wrapper = new QWidget;
  split = new QHBoxLayout(split_wrapper);
  split->setContentsMargins(0, 0, 0, 0);
  split->setSpacing(0);
  split->addWidget(nvg);

  if (getenv("DUAL_CAMERA_VIEW")) {
    CameraWidget *arCam = new CameraWidget("camerad", VISION_STREAM_NARROW_ROAD, this);
    split->insertWidget(0, arCam);
  }

  stacked_layout->addWidget(split_wrapper);

  alerts = new OnroadAlerts(this);
  alerts->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  stacked_layout->addWidget(alerts);

  // setup stacking order
  alerts->raise();

  setAttribute(Qt::WA_OpaquePaintEvent);

  // We handle the connection of the signals on the derived class
#ifndef SUNNYPILOT
  QObject::connect(uiState(), &UIState::uiUpdate, this, &OnroadWindow::updateState);
  QObject::connect(uiState(), &UIState::offroadTransition, this, &OnroadWindow::offroadTransition);
#endif
}

void OnroadWindow::updateState(const UIState &s) {
  if (!s.scene.started) {
    return;
  }

  alerts->updateState(s);
  nvg->updateState(s);

  QColor bgColor = bg_colors[s.status];
  if (bg != bgColor) {
    // repaint border
    bg = bgColor;
    update();
  }
}

void OnroadWindow::offroadTransition(bool offroad) {
  alerts->clear();
}

void OnroadWindow::paintEvent(QPaintEvent *event) {
  // Outer half of the 2026 raylib border. The status ring is a 30px stroke
  // centered on the camera rect edge, so only its outer 15px is visible here;
  // the inner 15px (and the rounded corners) is drawn by AnnotatedCameraWidget
  // in its own paintGL. Splitting it this way avoids compositing an extra
  // translucent widget over the QOpenGLWidget, which tanks the frame rate on
  // the C3's Wayland/EGL stack.
  QPainter p(this);
  p.fillRect(rect(), Qt::black);

  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(Qt::NoBrush);
  const QRectF ring = QRectF(rect()).adjusted(UI_BORDER_SIZE, UI_BORDER_SIZE, -UI_BORDER_SIZE, -UI_BORDER_SIZE);
  const qreal radius = borderRadius(ring.size());
  p.setPen(QPen(bg, UI_BORDER_SIZE, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  p.drawRoundedRect(ring, radius, radius);
}
