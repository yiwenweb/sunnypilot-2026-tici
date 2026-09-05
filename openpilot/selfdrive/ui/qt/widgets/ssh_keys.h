#pragma once

#include <QPushButton>

#include "openpilot/common/hardware/hw.h"

#ifdef SUNNYPILOT
#include "openpilot/selfdrive/ui/sunnypilot/qt/widgets/controls.h"
#define ButtonControl ButtonControlSP
#define ToggleControl ToggleControlSP
#else
#include "openpilot/selfdrive/ui/qt/widgets/controls.h"
#endif

// SSH enable toggle
// 2026 removed Hardware::{get,set}_ssh_enabled() (device SSH is managed
// out-of-band on AGNOS). Keep the row present but disabled instead of
// referencing a deleted API.
class SshToggle : public ToggleControl {
  Q_OBJECT

public:
  SshToggle() : ToggleControl(tr("Enable SSH"), "", "") {
    setEnabled(false);
  }
};

// SSH key management widget
class SshControl : public ButtonControl {
  Q_OBJECT

public:
  SshControl();

private:
  Params params;

  void refresh();
  void getUserKeys(const QString &username);
};
