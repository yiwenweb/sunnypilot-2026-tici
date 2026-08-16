#pragma once

#include <QWidget>

#include "openpilot/selfdrive/ui/ui.h"

class OnroadAlerts : public QWidget {
  Q_OBJECT

public:
  OnroadAlerts(QWidget *parent = 0) : QWidget(parent) {}
  void updateState(const UIState &s);
  void clear();

protected:
  struct Alert {
    QString text1;
    QString text2;
    QString type;
    cereal::SelfdriveState::AlertSize size;
    cereal::SelfdriveState::AlertStatus status;

    bool equal(const Alert &other) const {
      return text1 == other.text1 && text2 == other.text2 && type == other.type;
    }
  };

  // Matches raylib ALERT_COLORS (alert_renderer.py): normal=(0,0,0), userPrompt=(255,115,0), critical=(255,0,21)
  const QMap<cereal::SelfdriveState::AlertStatus, QColor> alert_colors = {
    {cereal::SelfdriveState::AlertStatus::NORMAL, QColor(0, 0, 0, 255)},
    {cereal::SelfdriveState::AlertStatus::USER_PROMPT, QColor(255, 115, 0, 255)},
    {cereal::SelfdriveState::AlertStatus::CRITICAL, QColor(255, 0, 21, 255)},
  };

  void paintEvent(QPaintEvent*) override;
  OnroadAlerts::Alert getAlert(const SubMaster &sm, uint64_t started_frame);

  QColor bg;
  Alert alert = {};
};
