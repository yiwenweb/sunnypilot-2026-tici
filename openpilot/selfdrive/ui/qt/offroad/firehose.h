#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include "openpilot/selfdrive/ui/qt/request_repeater.h"

#ifdef SUNNYPILOT
#include "openpilot/selfdrive/ui/sunnypilot/ui.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/widgets/controls.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/settings.h"
#else
#include "openpilot/selfdrive/ui/ui.h"
#include "openpilot/selfdrive/ui/qt/widgets/controls.h"
#include "openpilot/selfdrive/ui/qt/offroad/settings.h"
#endif

// Forward declarations
class SettingsWindow;

class FirehosePanel : public QWidget {
  Q_OBJECT
public:
  explicit FirehosePanel(SettingsWindow *parent);

private:
  QVBoxLayout *layout;

  QLabel *detailed_instructions;
  QLabel *contribution_label;
  QLabel *toggle_label;

  RequestRepeater *firehose_stats;

private slots:
  void refresh();
};
