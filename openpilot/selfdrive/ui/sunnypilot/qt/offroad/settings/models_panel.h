/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include <QLabel>
#include <QProgressBar>

#include "openpilot/selfdrive/ui/sunnypilot/qt/util.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/settings.h"

class ModelsPanel : public QWidget {
  Q_OBJECT

public:
  explicit ModelsPanel(QWidget *parent = nullptr);

private:
  // A lightweight view of a model bundle (live from modelManagerSP for the
  // active hardware source, or parsed from the cached JSON for the other).
  struct BundleInfo {
    QString ref;
    QString displayName;
    QString internalName;
    QString folder;
    int index = -1;
    int generation = 0;
  };

  QString activeSource() const;
  QString activeBundleKey(const QString &source) const;
  QString defaultModelName(const QString &source) const;
  QList<BundleInfo> bundlesForSource(const QString &source);
  QString slotBundleName(const QString &source);
  QString slotActiveRef(const QString &source);
  bool chestnutCompiled() const;
  QString statusNote();
  QPair<QString, QString> carryingModel();  // (source, name) of what actually drives
  void updateModelManagerState();
  void showEvent(QShowEvent *event) override;

  bool isDownloading() const {
    if (!model_manager.hasSelectedBundle()) {
        return false;
    }

    const auto &selected_bundle = model_manager.getSelectedBundle();
    return selected_bundle.getStatus() == cereal::ModelManagerSP::DownloadStatus::DOWNLOADING;
  }

  // UI update related methods
  void updateLabels();
  void handleModelSelectClicked(const QString &source);
  void handleBundleDownloadProgress();
  void refreshLaneTurnValueControl();
  void refreshCameraOffsetControl();
  void showResetParamsDialog();
  QProgressBar* createProgressBar(QWidget *parent);
  cereal::ModelManagerSP::Reader model_manager;
  cereal::ModelManagerSP::DownloadStatus download_status{};
  cereal::ModelManagerSP::DownloadStatus prev_download_status{};
  void clearModelCache();
  double calculateCacheSize();

  bool canContinueOnMeteredDialog() {
    if (!is_metered) return true;
    return showConfirmationDialog(QString(), QString(), is_metered);
  }

  inline bool showConfirmationDialog(const QString &message = QString(), const QString &confirmButtonText = QString(), const bool show_metered_warning = false) {
    return showConfirmationDialog(this, message, confirmButtonText, show_metered_warning);
  }

  static inline bool showConfirmationDialog(QWidget *parent, const QString &message = QString(), const QString &confirmButtonText = QString(), const bool show_metered_warning = false) {
    const QString warning_message = show_metered_warning ? tr("Warning: You are on a metered connection!") : QString();
    const QString final_message = QString("%1%2").arg(!message.isEmpty() ? message + "\n" : QString(), warning_message);
    const QString final_buttonText = !confirmButtonText.isEmpty() ? confirmButtonText : QString(tr("Continue") + " %1").arg(show_metered_warning ? tr("on Metered") : "");

    return ConfirmationDialog(final_message, final_buttonText, tr("Cancel"), true, parent).exec();
  }

  bool is_metered{};
  bool is_wifi{};
  bool is_onroad = false;

  // 缓存大小防抖
  double last_cache_calc_time{};

  ButtonControlSP *smallModelBtn;
  ButtonControlSP *bigModelBtn;
  ButtonControlSP *cancelDownloadBtn;
  ParamControlSP *lagd_toggle_control;
  OptionControlSP *delay_control;
  QProgressBar *downloadProgressBar;
  QFrame *downloadFrame;
  QLabel *downloadNoteLabel;  // failover note, mirrors _status_note() in the 2026-02 layout
  Params params;
  ButtonControlSP *clearModelCacheBtn;
  ButtonControlSP *refreshAvailableModelsBtn;
  ParamControlSP *lane_turn_desire_toggle;
  OptionControlSP *lane_turn_value_control;
  OptionControlSP *camera_offset_control = nullptr;
};
