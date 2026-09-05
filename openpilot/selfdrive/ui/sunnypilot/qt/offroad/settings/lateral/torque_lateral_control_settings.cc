/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/lateral/torque_lateral_control_settings.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <QJsonDocument>
#include <QJsonObject>

#include "openpilot/selfdrive/ui/qt/widgets/input.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/widgets/scrollview.h"

TorqueLateralControlSettings::TorqueLateralControlSettings(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(50, 20, 50, 20);
  main_layout->setSpacing(20);

  // Back button
  PanelBackButton *back = new PanelBackButton();
  connect(back, &QPushButton::clicked, [=]() { emit backPress(); });
  main_layout->addWidget(back, 0, Qt::AlignLeft);

  loadTorqueVersions();

  ListWidget *list = new ListWidget(this, false);

  // Torque Control Tune Version selector (matches raylib torque_settings.py)
  torqueVersionBtn = new ButtonControlSP(tr("Torque Control Tune Version"), tr("SELECT"),
                                         tr("Select the version of Torque Control Tune to use."), this);
  torqueVersionBtn->setValue(getCurrentTorqueVersionLabel());
  connect(torqueVersionBtn, &ButtonControlSP::clicked, this, &TorqueLateralControlSettings::showTorqueVersionDialog);
  list->addItem(torqueVersionBtn);

  // param, title, desc, icon
  std::vector<std::tuple<QString, QString, QString, QString>> toggle_defs{
    {
      "LiveTorqueParamsToggle",
      tr("Self-Tune"),
      tr("Enables self-tune for Torque lateral control for platforms that do not use Torque lateral control by default."),
      "../assets/offroad/icon_blank.png",
    },
    {
      "LiveTorqueParamsRelaxedToggle",
      tr("Less Restrict Settings for Self-Tune (Beta)"),
      tr("Less strict settings when using Self-Tune. This allows torqued to be more forgiving when learning values."),
      "../assets/offroad/icon_blank.png",
    }
  };

  for (auto &[param, title, desc, icon] : toggle_defs) {
    auto toggle = new ParamControlSP(param, title, desc, icon, this);
    list->addItem(toggle);
    toggles[param.toStdString()] = toggle;
  }

  torqueLateralControlCustomParams = new TorqueLateralControlCustomParams(
    "CustomTorqueParams",
    tr("Enable Custom Tuning"),
    tr("Enables custom tuning for Torque lateral control. Modifying Lateral Acceleration Factor and Friction below will override the offline values indicated in the YAML files within \"opendbc/car/torque_data\". "
       "The values will also be used live when \"Manual Real-Time Tuning\" toggle is enabled."),
    "../assets/offroad/icon_blank.png",
    this);
  list->addItem(torqueLateralControlCustomParams);

  QObject::connect(uiState(), &UIState::offroadTransition, this, &TorqueLateralControlSettings::updateToggles);
  QObject::connect(toggles["LiveTorqueParamsToggle"], &ParamControlSP::toggleFlipped, [=](bool state) {
    if (!state) {
      params.remove("LiveTorqueParamsRelaxedToggle");
      toggles["LiveTorqueParamsRelaxedToggle"]->refresh();
    }

    updateToggles(offroad);
  });

  main_layout->addWidget(new ScrollViewSP(list, this));
}

void TorqueLateralControlSettings::showEvent(QShowEvent *event) {
  updateToggles(offroad);
}

void TorqueLateralControlSettings::updateToggles(bool _offroad) {
  bool live_toggle = toggles["LiveTorqueParamsToggle"]->isToggled();

  toggles["LiveTorqueParamsToggle"]->setEnabled(_offroad);
  toggles["LiveTorqueParamsRelaxedToggle"]->setEnabled(_offroad && live_toggle);

  torqueLateralControlCustomParams->setEnabled(_offroad);
  torqueLateralControlCustomParams->refresh();

  offroad = _offroad;
}

void TorqueLateralControlSettings::loadTorqueVersions() {
  torqueVersions.clear();

  const QString path = "/data/openpilot/openpilot/sunnypilot/selfdrive/controls/lib/latcontrol_torque_versions.json";
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return;
  }

  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  file.close();

  if (!doc.isObject()) {
    return;
  }

  const QJsonObject obj = doc.object();
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    const QString label = it.key();
    const QJsonObject info = it.value().toObject();
    if (info.contains("version")) {
      torqueVersions[label.toStdString()] = static_cast<float>(info["version"].toDouble());
    }
  }
}

QString TorqueLateralControlSettings::getCurrentTorqueVersionLabel() {
  const std::string val = params.get("TorqueControlTune");
  if (val.empty()) {
    return tr("Default");
  }

  try {
    const float current = std::stof(val);
    for (const auto &[label, version] : torqueVersions) {
      if (std::abs(version - current) < 1e-5f) {
        return QString::fromStdString(label);
      }
    }
  } catch (...) {
  }

  return tr("Default");
}

void TorqueLateralControlSettings::showTorqueVersionDialog() {
  QStringList options;
  options << tr("Default");

  // Sort labels by version descending (match raylib)
  std::vector<std::pair<float, QString>> sorted;
  for (const auto &[label, version] : torqueVersions) {
    sorted.emplace_back(version, QString::fromStdString(label));
  }
  std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
  for (const auto &[version, label] : sorted) {
    options << label;
  }

  const QString cur = getCurrentTorqueVersionLabel();
  const QString selection = MultiOptionDialog::getSelection(tr("Select Torque Control Tune Version"), options, cur, this);

  if (selection.isEmpty()) {
    return;
  }

  if (selection == tr("Default")) {
    params.remove("TorqueControlTune");
  } else {
    const auto it = torqueVersions.find(selection.toStdString());
    if (it != torqueVersions.end()) {
      params.put("TorqueControlTune", std::to_string(it->second));
    }
  }

  torqueVersionBtn->setValue(getCurrentTorqueVersionLabel());
}
