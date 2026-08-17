/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/service_panel.h"

#include <cmath>

#include "openpilot/cereal/messaging/messaging.h"

ServicePanelSP::ServicePanelSP(QWidget *parent) : QFrame(parent) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setMargin(0);
  main_layout->setSpacing(0);

  main_layout->addSpacing(25);

  // Device mounting calibration values (pitch / yaw / height) with limits
  mountingPitch = new ValueRowSP(tr("Mounting Pitch"));
  mountingYaw = new ValueRowSP(tr("Mounting Yaw"));
  mountingHeight = new ValueRowSP(tr("Mounting Height"));
  calibrationStatus = new ValueRowSP(tr("Calibration Status"));
  calibrationProgress = new ValueRowSP(tr("Calibration Progress"));

  main_layout->addWidget(mountingPitch);
  main_layout->addWidget(mountingYaw);
  main_layout->addWidget(mountingHeight);
  main_layout->addWidget(calibrationStatus);
  main_layout->addWidget(calibrationProgress);

  main_layout->addStretch(1);

  setStyleSheet(R"(
    * {
      color: white;
    }
    ServicePanelSP > QLabel {
      font-size: 55px;
    }
  )");

  refresh();
}

void ServicePanelSP::showEvent(QShowEvent *event) {
  refresh();
}

void ServicePanelSP::refresh() {
  const std::string calib_bytes = params.get("CalibrationParams");

  if (calib_bytes.empty()) {
    mountingPitch->setValue(tr("N/A"));
    mountingYaw->setValue(tr("N/A"));
    mountingHeight->setValue(tr("N/A"));
    calibrationStatus->setValue(tr("Uncalibrated"));
    calibrationProgress->setValue(tr("0%"));
    return;
  }

  try {
    AlignedBuffer aligned_buf;
    capnp::FlatArrayMessageReader cmsg(aligned_buf.align(calib_bytes.data(), calib_bytes.size()));
    auto calib = cmsg.getRoot<cereal::Event>().getLiveCalibration();

    // Pitch / yaw limits for TICI (matches calibrationd.py)
    constexpr double pitch_min = -0.09074112085129739;
    constexpr double pitch_max = 0.17;
    constexpr double yaw_limit = 0.06912048084718224;

    double pitch_rad = calib.getRpyCalib()[1];
    double yaw_rad = calib.getRpyCalib()[2];
    double pitch_deg = pitch_rad * (180.0 / M_PI);
    double yaw_deg = yaw_rad * (180.0 / M_PI);

    bool pitch_ok = (pitch_rad > pitch_min) && (pitch_rad < pitch_max);
    bool yaw_ok = (std::abs(yaw_rad) < yaw_limit);

    // Mounting Pitch
    mountingPitch->setValue(QString("%1° %2")
      .arg(QString::number(std::abs(pitch_deg), 'f', 1))
      .arg(pitch_deg > 0 ? tr("down") : tr("up")),
      pitch_ok ? QString("#00ffcc") : QString("#ff4d4d"));
    mountingPitch->setDescription(tr("Limit: %1° up to %2° down.")
      .arg(QString::number(std::abs(pitch_min * (180.0 / M_PI)), 'f', 1))
      .arg(QString::number(pitch_max * (180.0 / M_PI), 'f', 1)));

    // Mounting Yaw
    mountingYaw->setValue(QString("%1° %2")
      .arg(QString::number(std::abs(yaw_deg), 'f', 1))
      .arg(yaw_deg > 0 ? tr("left") : tr("right")),
      yaw_ok ? QString("#00ffcc") : QString("#ff4d4d"));
    mountingYaw->setDescription(tr("Limit: ±%1°.").arg(QString::number(yaw_limit * (180.0 / M_PI), 'f', 1)));

    // Mounting Height
    double height = calib.getHeight()[0];
    mountingHeight->setValue(QString("%1 m").arg(QString::number(height, 'f', 2)));

    // Calibration Status
    QString status_text;
    switch (calib.getCalStatus()) {
      case cereal::LiveCalibrationData::Status::CALIBRATED:
        status_text = tr("Calibrated");
        break;
      case cereal::LiveCalibrationData::Status::INVALID:
        status_text = tr("Invalid");
        break;
      case cereal::LiveCalibrationData::Status::RECALIBRATING:
        status_text = tr("Recalibrating");
        break;
      default:
        status_text = tr("Uncalibrated");
        break;
    }
    calibrationStatus->setValue(status_text,
      calib.getCalStatus() == cereal::LiveCalibrationData::Status::CALIBRATED ? QString("#00ffcc") : QString("#ffb400"));

    // Calibration Progress
    calibrationProgress->setValue(QString("%1%").arg(calib.getCalPerc()));
  } catch (kj::Exception) {
    mountingPitch->setValue(tr("N/A"));
    mountingYaw->setValue(tr("N/A"));
    mountingHeight->setValue(tr("N/A"));
    calibrationStatus->setValue(tr("Error"));
    calibrationProgress->setValue(tr("N/A"));
  }
}
