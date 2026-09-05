#pragma once

#include "openpilot/selfdrive/ui/qt/widgets/cameraview.h"
#include "openpilot/selfdrive/ui/qt/onroad/driver_monitoring.h"

class DriverViewWindow : public CameraWidget {
  Q_OBJECT

public:
  explicit DriverViewWindow(QWidget *parent);

signals:
  void done();

protected:
  mat4 calcFrameMatrix() override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void paintGL() override;

  Params params;
  DriverMonitorRenderer driver_monitor;
};
