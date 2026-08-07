#pragma once

#ifdef SUNNYPILOT
#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/settings.h"
#else
#include "openpilot/selfdrive/ui/qt/offroad/settings.h"
#endif

class DeveloperPanel : public ListWidget {
  Q_OBJECT
public:
  explicit DeveloperPanel(SettingsWindow *parent);
  void showEvent(QShowEvent *event) override;

protected:
  Params params;
  ParamControl* adbToggle;
  ParamControl* joystickToggle;
  ParamControl* longManeuverToggle;
  ParamControl* experimentalLongitudinalToggle;
  bool is_release;
  bool offroad = false;

private slots:
  void updateToggles(bool _offroad);
};
