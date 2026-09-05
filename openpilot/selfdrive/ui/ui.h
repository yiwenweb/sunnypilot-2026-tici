#pragma once

#include <algorithm>
#include <eigen3/Eigen/Dense>
#include <memory>
#include <string>

#include <QTimer>
#include <QColor>
#include <QFuture>
#include <QSizeF>

#include "openpilot/cereal/messaging/messaging.h"
#include "openpilot/common/mat.h"
#include "openpilot/common/params.h"
#include "openpilot/common/util.h"
#include "openpilot/common/hardware/hw.h"
#include "openpilot/selfdrive/ui/qt/prime_state.h"

const int UI_BORDER_SIZE = 30;
const int UI_HEADER_HEIGHT = 420;

const int UI_FREQ = 20; // Hz
const int BACKLIGHT_OFFROAD = 50;

const Eigen::Matrix3f VIEW_FROM_DEVICE = (Eigen::Matrix3f() <<
  0.0, 1.0, 0.0,
  0.0, 0.0, 1.0,
  1.0, 0.0, 0.0).finished();

const Eigen::Matrix3f FCAM_INTRINSIC_MATRIX = (Eigen::Matrix3f() <<
  2648.0, 0.0, 1928.0 / 2,
  0.0, 2648.0, 1208.0 / 2,
  0.0, 0.0, 1.0).finished();

// tici ecam focal probably wrong? magnification is not consistent across frame
// Need to retrain model before this can be changed
const Eigen::Matrix3f ECAM_INTRINSIC_MATRIX = (Eigen::Matrix3f() <<
  567.0, 0.0, 1928.0 / 2,
  0.0, 567.0, 1208.0 / 2,
  0.0, 0.0, 1.0).finished();

typedef enum UIStatus {
  STATUS_DISENGAGED,
  STATUS_OVERRIDE,
  STATUS_ENGAGED,
  STATUS_LAT_ONLY,
  STATUS_LONG_ONLY,
} UIStatus;

// Border colors, aligned with the 2026 raylib UI (BORDER_COLORS / BORDER_COLORS_SP).
// All fully opaque; the border is drawn as a rounded ring on top of a black frame.
const QColor bg_colors [] = {
  [STATUS_DISENGAGED] = QColor(0x12, 0x28, 0x39, 0xff),
  [STATUS_OVERRIDE] = QColor(0x89, 0x92, 0x8D, 0xff),
  [STATUS_ENGAGED] = QColor(0x16, 0x7F, 0x40, 0xff),
  [STATUS_LAT_ONLY] = QColor(0x00, 0xC8, 0xC8, 0xff),
  [STATUS_LONG_ONLY] = QColor(0x96, 0x1C, 0xA8, 0xff),
};

// Rounded border geometry, matching raylib's draw_rectangle_rounded_lines_ex()
// (roundness is relative to half of the shorter side of the border rect).
const float UI_BORDER_ROUNDNESS = 0.12f;

// Corner radius of the status border ring, for a border rect of the given size.
// Shared by OnroadWindow (outer half of the stroke) and AnnotatedCameraWidget
// (inner half), so the two halves line up.
inline qreal borderRadius(const QSizeF &border_size) {
  return UI_BORDER_ROUNDNESS * std::min(border_size.width(), border_size.height()) / 2.0;
}

typedef struct UIScene {
  Eigen::Matrix3f view_from_calib = VIEW_FROM_DEVICE;
  Eigen::Matrix3f view_from_wide_calib = VIEW_FROM_DEVICE;
  cereal::PandaState::PandaType pandaType;

  cereal::LongitudinalPersonality personality;

  float light_sensor = -1;
  bool started, ignition, is_metric, recording_audio;
  uint64_t started_frame;
} UIScene;

#ifdef SUNNYPILOT
#include "sunnypilot/ui_scene.h"
#define UIScene UISceneSP
#endif

class UIState : public QObject {
  Q_OBJECT

public:
  UIState(QObject* parent = 0);
  virtual void updateStatus();
  virtual inline bool engaged() const {
    return scene.started && (*sm)["selfdriveState"].getSelfdriveState().getEnabled();
  }

  std::unique_ptr<SubMaster> sm;
  UIStatus status;
  UIScene scene = {};
  QString language;
  PrimeState *prime_state;

signals:
  void uiUpdate(const UIState &s);
  void offroadTransition(bool offroad);
  void engagedChanged(bool engaged);

protected slots:
  virtual void update();

protected:
  QTimer *timer;

private:
  bool started_prev = false;
  bool engaged_prev = false;
};

#ifndef SUNNYPILOT
UIState *uiState();
#endif

// device management class
class Device : public QObject {
  Q_OBJECT

public:
  Device(QObject *parent = 0);
  bool isAwake() { return awake; }
  void setOffroadBrightness(int brightness) {
    offroad_brightness = std::clamp(brightness, 0, 100);
  }

protected:
  bool awake = false;
  int interactive_timeout = 0;
  bool ignition_on = false;

  int offroad_brightness = BACKLIGHT_OFFROAD;
  int last_brightness = 0;
  FirstOrderFilter brightness_filter;
  QFuture<void> brightness_future;

  void updateBrightness(const UIState &s);
  void updateWakefulness(const UIState &s);
  void setAwake(bool on);

signals:
  void displayPowerChanged(bool on);
  void interactiveTimeout();

public slots:
  void resetInteractiveTimeout(int timeout = -1);
  void update(const UIState &s);
};

#ifndef SUNNYPILOT
Device *device();
#endif

void ui_update_params(UIState *s);
void update_state(UIState *s);
void update_sockets(UIState *s);
