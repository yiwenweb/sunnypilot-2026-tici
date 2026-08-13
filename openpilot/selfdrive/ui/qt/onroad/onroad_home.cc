#include "openpilot/selfdrive/ui/qt/onroad/onroad_home.h"

#include <algorithm>

#include <QPainter>
#include <QPainterPath>
#include <QStackedLayout>

#include "openpilot/selfdrive/ui/qt/util.h"

// OnroadBorder: black outer frame + rounded colored inner ring.
// Mirrors the raylib implementation:
//   rl.draw_rectangle_lines_ex(rect, UI_BORDER_SIZE, rl.BLACK)              -> frame inside 0..30px
//   rl.draw_rectangle_rounded_lines_ex(inner, 0.12, 10, UI_BORDER_SIZE, c)  -> stroke centered on
//                                                                             the inner rect edge
OnroadBorder::OnroadBorder(QWidget *parent) : QWidget(parent) {
  // Non-opaque child widget: unpainted regions keep whatever the camera view
  // below has drawn, same as how OnroadAlerts overlays the camera.
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void OnroadBorder::setColor(const QColor &c) {
  if (color != c) {
    color = c;
    update();
  }
}

void OnroadBorder::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(Qt::NoBrush);

  // Black outer frame, drawn inside the widget bounds (raylib draw_rectangle_lines_ex semantics)
  const qreal half = UI_BORDER_SIZE / 2.0;
  p.setPen(QPen(Qt::black, UI_BORDER_SIZE, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  p.drawRect(QRectF(rect()).adjusted(half, half, -half, -half));

  // Rounded status ring, stroke centered on the content rect edge
  const QRectF border_rect = QRectF(rect()).adjusted(UI_BORDER_SIZE, UI_BORDER_SIZE, -UI_BORDER_SIZE, -UI_BORDER_SIZE);
  const qreal radius = UI_BORDER_ROUNDNESS * std::min(border_rect.width(), border_rect.height()) / 2.0;
  p.setPen(QPen(color, UI_BORDER_SIZE, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  p.drawRoundedRect(border_rect, radius, radius);
}

OnroadWindow::OnroadWindow(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *main_layout  = new QVBoxLayout(this);
  main_layout->setMargin(UI_BORDER_SIZE);
  QStackedLayout *stacked_layout = new QStackedLayout;
  stacked_layout->setStackingMode(QStackedLayout::StackAll);
  main_layout->addLayout(stacked_layout);

  nvg = new AnnotatedCameraWidget(VISION_STREAM_ROAD, this);

  QWidget * split_wrapper = new QWidget;
  split = new QHBoxLayout(split_wrapper);
  split->setContentsMargins(0, 0, 0, 0);
  split->setSpacing(0);
  split->addWidget(nvg);

  if (getenv("DUAL_CAMERA_VIEW")) {
    CameraWidget *arCam = new CameraWidget("camerad", VISION_STREAM_ROAD, this);
    split->insertWidget(0, arCam);
  }

  stacked_layout->addWidget(split_wrapper);

  alerts = new OnroadAlerts(this);
  alerts->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  stacked_layout->addWidget(alerts);

  // Border is not part of the layout: it spans the full window, including the
  // UI_BORDER_SIZE band around the camera view, and paints on top of everything.
  border = new OnroadBorder(this);

  // setup stacking order
  alerts->raise();
  border->raise();

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
    border->setColor(bg);
    update();
  }
}

void OnroadWindow::offroadTransition(bool offroad) {
  alerts->clear();
}

void OnroadWindow::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  border->setGeometry(rect());
}

void OnroadWindow::paintEvent(QPaintEvent *event) {
  // Background behind the camera view. The visible border itself is painted by
  // OnroadBorder on top, so keep this black to match the raylib black frame.
  QPainter p(this);
  p.fillRect(rect(), Qt::black);
}
