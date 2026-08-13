#pragma once

#include <QResizeEvent>

#include "openpilot/selfdrive/ui/qt/onroad/alerts.h"

#ifdef SUNNYPILOT
#include "openpilot/selfdrive/ui/sunnypilot/qt/onroad/annotated_camera.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/onroad/alerts.h"
#define UIState UIStateSP
#define AnnotatedCameraWidget AnnotatedCameraWidgetSP
#define OnroadAlerts OnroadAlertsSP
#else
#include "openpilot/selfdrive/ui/qt/onroad/annotated_camera.h"
#endif

// Rounded status border drawn on top of the camera view, replicating the
// 2026 raylib UI: a black outer frame with a rounded, colored inner ring.
// See AugmentedRoadView._draw_border() in selfdrive/ui/onroad/augmented_road_view.py.
class OnroadBorder : public QWidget {
  Q_OBJECT

public:
  explicit OnroadBorder(QWidget *parent = nullptr);
  void setColor(const QColor &c);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QColor color = bg_colors[STATUS_DISENGAGED];
};

class OnroadWindow : public QWidget {
  Q_OBJECT

public:
  OnroadWindow(QWidget* parent = 0);

protected:
  void paintEvent(QPaintEvent *event);
  void resizeEvent(QResizeEvent *event) override;
  OnroadAlerts *alerts;
  OnroadBorder *border;
  AnnotatedCameraWidget *nvg;
  QColor bg = bg_colors[STATUS_DISENGAGED];
  QHBoxLayout* split;

protected slots:
  virtual void offroadTransition(bool offroad);
  virtual void updateState(const UIState &s);
};
