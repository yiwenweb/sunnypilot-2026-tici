#pragma once

#include <QPainter>
#include <QVBoxLayout>
#include <memory>
#include "openpilot/selfdrive/ui/qt/onroad/driver_monitoring.h"
#include "openpilot/selfdrive/ui/qt/onroad/model.h"
#include "openpilot/selfdrive/ui/qt/widgets/cameraview.h"

#ifdef SUNNYPILOT
#include "openpilot/selfdrive/ui/sunnypilot/qt/onroad/buttons.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/onroad/hud.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/onroad/model.h"
#define ExperimentalButton ExperimentalButtonSP
#define ModelRenderer ModelRendererSP
#define HudRenderer HudRendererSP
#else
#include "openpilot/selfdrive/ui/qt/onroad/buttons.h"
#include "openpilot/selfdrive/ui/qt/onroad/hud.h"
#endif

class AnnotatedCameraWidget : public CameraWidget {
  Q_OBJECT

public:
  explicit AnnotatedCameraWidget(VisionStreamType type, QWidget* parent = 0);
  virtual ~AnnotatedCameraWidget() = default;
  virtual void updateState(const UIState &s);

private:
  QVBoxLayout *main_layout;
  ExperimentalButton *experimental_btn;
  DriverMonitorRenderer dmon;
  HudRenderer hud;
  ModelRenderer model;
  std::unique_ptr<PubMaster> pm;

  int skip_frame_count = 0;
  bool wide_cam_requested = false;

protected:
  void paintGL() override;
  void initializeGL() override;
  void showEvent(QShowEvent *event) override;
  mat4 calcFrameMatrix() override;

  // Bottom fade-out overlay, drawn between the model and the HUD.
  // No-op in stock; sunnypilot overrides it (see AugmentedRoadViewSP).
  virtual void drawFadeOverlay(QPainter &p, const QRect &surface_rect) {}

  double prev_draw_t = 0;
  FirstOrderFilter fps_filter;
};
