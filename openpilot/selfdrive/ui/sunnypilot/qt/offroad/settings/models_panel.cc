/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include <algorithm>
#include <chrono>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStyle>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <QDir>
#include "openpilot/common/model.h"

#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/models_panel.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/widgets/scrollview.h"

static const QString progressStyleActive = "QProgressBar {"
    "  font-size: 40px;"
    "  font-weight: 200;"
    "  padding: 1px;"
    "  border: 3px solid black;"
    "  border-radius: 10px;"
    "}"
    "QProgressBar::chunk {"
    "  background-color: #1e79e8;"
    "  border-radius: 10px;"
    "}";

static const QString progressStyleInactive = progressStyleActive +
    "QProgressBar::chunk {"
    "  background-color: transparent;"
    "}";

static const QString progressStyleDone = progressStyleActive +
    "QProgressBar {"
    "  color: #33ab4c;"
    "}"
    "QProgressBar::chunk {"
    "  background-color: transparent;"
    "}";

static const QString progressStyleError = progressStyleActive +
    "QProgressBar {"
    "  color: red;"
    "}"
    "QProgressBar::chunk {"
    "  background-color: transparent;"
    "}";

ModelsPanel::ModelsPanel(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(50, 20, 50, 20);

  ListWidgetSP *list = new ListWidgetSP(this, false);
  ScrollViewSP *scroller = new ScrollViewSP(list, this);
  main_layout->addWidget(scroller);

  // Dual-slot selectors, matching the 2026-02 raylib UI:
  // "qcom" drives the small model slot, "chestnut" the big model slot.
  smallModelBtn = new ButtonControlSP(tr("Small Model"), tr("SELECT"), "", this);
  bigModelBtn = new ButtonControlSP(tr("Big Model"), tr("SELECT"), "", this);
  smallModelBtn->setValue(slotBundleName("qcom"));
  bigModelBtn->setValue(slotBundleName("chestnut"));

  connect(smallModelBtn, &ButtonControlSP::clicked, this, [this]() { handleModelSelectClicked("qcom"); });
  connect(bigModelBtn, &ButtonControlSP::clicked, this, [this]() { handleModelSelectClicked("chestnut"); });
  connect(uiState(), &UIState::offroadTransition, [=](bool offroad) {
      is_onroad = !offroad;
      updateLabels();
    });
  connect(uiStateSP(), &UIStateSP::uiUpdate, this, &ModelsPanel::updateLabels);
  list->addItem(smallModelBtn);
  list->addItem(bigModelBtn);

  // Item order mirrors the 2026-02 raylib models layout:
  // small, big, cancel download, model status, refresh, clear cache, then toggles.
  cancelDownloadBtn = new ButtonControlSP(tr("Cancel Download"), tr("CANCEL"), "", this);
  cancelDownloadBtn->setVisible(false);
  connect(cancelDownloadBtn, &ButtonControlSP::clicked, [=]() {
    params.remove("ModelManager_DownloadRef");
  });
  list->addItem(cancelDownloadBtn);

  // Single "Model Status" row (replaces the old per-artifact progress frames),
  // with the failover note rendered below it.
  downloadFrame = new QFrame(this);
  QVBoxLayout *status_layout = new QVBoxLayout(downloadFrame);
  status_layout->setContentsMargins(0, 0, 0, 0);
  status_layout->setSpacing(5);
  QHBoxLayout *status_row = new QHBoxLayout();
  status_row->setContentsMargins(0, 0, 0, 0);
  status_row->setSpacing(50);
  status_row->addWidget(new QLabel(tr("Model Status")));
  downloadProgressBar = createProgressBar(this);
  status_row->addWidget(downloadProgressBar);
  status_layout->addLayout(status_row);
  downloadNoteLabel = new QLabel(downloadFrame);
  downloadNoteLabel->setWordWrap(true);
  downloadNoteLabel->setStyleSheet("color: #b8bec6; font-size: 45px;");
  downloadNoteLabel->setVisible(false);
  status_layout->addWidget(downloadNoteLabel);
  downloadFrame->setVisible(false);
  list->addItem(downloadFrame);

  refreshAvailableModelsBtn = new ButtonControlSP(tr("Refresh Model List"), tr("REFRESH"), "", this);
  connect(refreshAvailableModelsBtn, &ButtonControlSP::clicked, this, [=]() {
    params.put("ModelManager_LastSyncTime", "0");
    params.put("ModelManager_LastSyncTime_Chestnut", "0");
    ConfirmationDialog::alert(tr("Fetching Latest Models"), this);
  });

  list->addItem(refreshAvailableModelsBtn);

  clearModelCacheBtn = new ButtonControlSP(tr("Clear Model Cache"), tr("CLEAR"), "", this);
  connect(clearModelCacheBtn, &ButtonControlSP::clicked, this, &ModelsPanel::clearModelCache);

  list->addItem(clearModelCacheBtn);
  list->addItem(horizontal_line());

  // Lane Turn Desire toggle
  lane_turn_desire_toggle = new ParamControlSP("LaneTurnDesire", tr("Use Lane Turn Desires"),
                            "If you’re driving at 20 mph (32 km/h) or below and have your blinker on, "
                            "the car will plan a turn in that direction at the nearest drivable path. "
                            "This prevents situations (like at red lights) where the car might plan the wrong turn direction.",
                             "../assets/icons/shell.png");
  list->addItem(lane_turn_desire_toggle);

  // Lane Turn Value control
  int max_value_mph = 20;
  bool is_metric_initial = params.getBool("IsMetric");
  const float K = 1.609344f;
  int per_value_change_scaled = is_metric_initial ? static_cast<int>(std::round((1.0f / K) * 100.0f)) : 100; // 100 -> 1 mph
  lane_turn_value_control = new OptionControlSP("LaneTurnValue", tr("Adjust Lane Turn Speed"),
    tr("Set the maximum speed for lane turn desires. Default is 19 %1.").arg(is_metric_initial ? "km/h" : "mph"),
    "", {5 * 100, max_value_mph * 100}, per_value_change_scaled, false, nullptr, true, true);
  lane_turn_value_control->showDescription();
  list->addItem(lane_turn_value_control);

  // Show based on toggle
  refreshLaneTurnValueControl();
  connect(lane_turn_desire_toggle, &ParamControlSP::toggleFlipped, this, &ModelsPanel::refreshLaneTurnValueControl);
  connect(lane_turn_value_control, &OptionControlSP::updateLabels, this, &ModelsPanel::refreshLaneTurnValueControl);

  // LiveDelay toggle
  lagd_toggle_control = new ParamControlSP("LagdToggle", tr("Live Learning Steer Delay"), "", "../assets/icons/shell.png");
  lagd_toggle_control->showDescription();
  list->addItem(lagd_toggle_control);

  // Software delay control
  delay_control = new OptionControlSP("LagdToggleDelay", tr("Adjust Software Delay"),
                                      tr("Adjust the software delay when Live Learning Steer Delay is toggled off."
                                         "\nThe default software delay value is 0.2"),
                                      "", {5, 50}, 1, false, nullptr, true, true);

  connect(delay_control, &OptionControlSP::updateLabels, [=]() {
    float value = QString::fromStdString(params.get("LagdToggleDelay")).toFloat();
    delay_control->setLabel(QString::number(value, 'f', 2) + "s");
  });
  connect(lagd_toggle_control, &ParamControlSP::toggleFlipped, [=](bool state) {
    delay_control->setVisible(!state && params.getBool("ShowAdvancedControls"));
  });
  delay_control->showDescription();
  list->addItem(delay_control);

  // Camera Offset: virtually shifts the camera's perspective, moving the model's
  // center left (+) or right (-). Stored in meters; the control works in
  // centimeter steps and OptionControlSP's float scaling writes back x/100.
  // Range matches settings_ui_src/pages/models.yaml (-0.35..0.35, step 0.01).
  camera_offset_control = new OptionControlSP("CameraOffset", tr("Adjust Camera Offset"),
    tr("Virtually shift camera's perspective to move model's center to Left (+ values) or Right (- values)."),
    "", {-35, 35}, 1, false, nullptr, true, true);
  connect(camera_offset_control, &OptionControlSP::updateLabels, this, &ModelsPanel::refreshCameraOffsetControl);
  camera_offset_control->showDescription();
  list->addItem(camera_offset_control);
  refreshCameraOffsetControl();
}

void ModelsPanel::refreshCameraOffsetControl() {
  if (!camera_offset_control) return;
  const float value = QString::fromStdString(params.get("CameraOffset")).toFloat();
  camera_offset_control->setLabel(QString::number(value, 'f', 2) + " m");
  // Baseline shows the offset control only while a custom bundle is driving
  // (ui_state.active_bundle is not None), same advanced gating as the others.
  const bool custom_active = slotActiveRef(activeSource()) != DEFAULT_MODEL;
  camera_offset_control->setVisible(params.getBool("ShowAdvancedControls") && custom_active);
}

QProgressBar* ModelsPanel::createProgressBar(QWidget *parent) {
  QProgressBar *progressBar = new QProgressBar(parent);
  progressBar->setRange(0, 100);
  progressBar->setValue(0);
  progressBar->setTextVisible(true);
  progressBar->setAlignment(Qt::AlignVCenter);
  return progressBar;
}

// big_driving_tinygrad.pkl.chunkmanifest present == the chestnut runner is
// compiled; mirrors chestnut_compiled() in selfdrive/modeld/helpers.py.
bool ModelsPanel::chestnutCompiled() const {
  return QFileInfo::exists("selfdrive/modeld/models/big_driving_tinygrad.pkl.chunkmanifest");
}

void ModelsPanel::refreshLaneTurnValueControl() {
  if (!lane_turn_value_control) return;
  float stored_mph = QString::fromStdString(params.get("LaneTurnValue")).toFloat();
  bool is_metric = params.getBool("IsMetric");
  QString unit = is_metric ? "km/h" : "mph";
  float display_value = stored_mph;
  if (is_metric) {
    display_value = stored_mph * 1.609344f;
  }
  lane_turn_value_control->setLabel(QString::number(static_cast<int>(std::round(display_value))) + " " + unit);
  lane_turn_value_control->setVisible(params.getBool("LaneTurnDesire") && params.getBool("ShowAdvancedControls"));
}

/**
 * @brief Updates the single "Model Status" row, mirroring _handle_bundle_download_progress()
 * and _download_row_state() from the 2026-02 raylib layout.
 */
void ModelsPanel::handleBundleDownloadProgress() {
  using DS = cereal::ModelManagerSP::DownloadStatus;
  downloadFrame->setVisible(false);
  downloadNoteLabel->setVisible(false);

  // Cancel Download 按钮显隐
  cancelDownloadBtn->setVisible(
    model_manager.hasSelectedBundle() &&
    !params.get("ModelManager_DownloadRef").empty()
  );

  // 缓存大小 0.5s 防抖
  const double current_time = std::chrono::duration<double>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  if (current_time - last_cache_calc_time > 0.5) {
    last_cache_calc_time = current_time;
    clearModelCacheBtn->setValue(QString::number(calculateCacheSize(), 'f', 2) + " MB");
  }

  if (!model_manager.hasSelectedBundle() && !model_manager.hasActiveBundle()) {
    return;
  }

  const bool showSelectedBundle = model_manager.hasSelectedBundle() && (isDownloading() || model_manager.getSelectedBundle().getStatus() == DS::FAILED);
  const auto &bundle = showSelectedBundle ? model_manager.getSelectedBundle() : model_manager.getActiveBundle();
  download_status = bundle.getStatus();
  const bool download_status_changed = prev_download_status != download_status;
  prev_download_status = download_status;

  // Aggregate every artifact of the bundle, like the baseline does
  bool any_downloading = false, any_verifying = false, any_failed = false;
  float progress_sum = 0.0f;
  int artifact_count = 0;
  for (const auto &model : bundle.getModels()) {
    const auto &artifact = model.getArtifact();
    if (artifact.getFileName().size() == 0) {
      continue;
    }
    const auto &progress = artifact.getDownloadProgress();
    progress_sum += progress.getProgress();
    ++artifact_count;
    switch (progress.getStatus()) {
      case DS::DOWNLOADING: any_downloading = true; break;
      case DS::VERIFYING:   any_verifying = true;   break;
      case DS::FAILED:      any_failed = true;      break;
      default: break;
    }
  }
  const float progress_avg = artifact_count > 0 ? progress_sum / artifact_count : 0.0f;
  const QString bundle_name = QString::fromStdString(bundle.getInternalName());

  downloadFrame->setVisible(true);

  if (any_failed) {
    downloadProgressBar->setStyleSheet(progressStyleError);
    downloadProgressBar->setFormat(tr("  %1 - download failed").arg(bundle_name));
  } else if (any_verifying) {
    downloadProgressBar->setStyleSheet(progressStyleActive);
    downloadProgressBar->setValue(static_cast<int>(progress_avg));
    downloadProgressBar->setFormat(tr("  %1 - verifying").arg(bundle_name));
  } else if (any_downloading) {
    downloadProgressBar->setStyleSheet(progressStyleActive);
    downloadProgressBar->setValue(static_cast<int>(progress_avg));
    downloadProgressBar->setFormat("  %1% - " + bundle_name);
    device()->resetInteractiveTimeout();
  } else if (showSelectedBundle) {
    // selected but the manager hasn't picked it up yet
    downloadProgressBar->setStyleSheet(progressStyleInactive);
    downloadProgressBar->setFormat(tr("  pending - %1").arg(bundle_name));
  } else if (download_status == DS::DOWNLOADED || download_status == DS::CACHED) {
    downloadProgressBar->setStyleSheet(progressStyleDone);
    downloadProgressBar->setFormat(tr("  %1 - %2").arg(bundle_name, download_status_changed ? tr("downloaded") : tr("ready")));
  } else {
    // Idle: show both slots side by side like _slot_segments() in the baseline
    downloadProgressBar->setStyleSheet(progressStyleInactive);
    downloadProgressBar->setFormat(tr("  small: %1   |   big: %2").arg(slotBundleName("qcom"), slotBundleName("chestnut")));
  }

  // Failover note, mirrors _status_note() (offroad-only states apply here)
  const QString note = statusNote();
  if (!note.isEmpty()) {
    downloadNoteLabel->setText(note);
    downloadNoteLabel->setVisible(true);
  }
}

/**
 * @brief The failover story for the Model Status row. One-way big -> small, and
 * the fallback is runner-matched. Mirrors _status_note() in the 2026-02 layout.
 */
QString ModelsPanel::statusNote() {
  const SubMaster &sm = *(uiStateSP()->sm);
  const bool chestnut_present = sm["deviceState"].getDeviceState().getChestnutPresent();
  if (!chestnut_present) {
    return QString();
  }

  const bool big_is_default = slotActiveRef("chestnut") == DEFAULT_MODEL;
  if (!chestnutCompiled()) {
    // ChestnutState.UNCOMPILED -> 'failed' in big_model_state()
    if (big_is_default) {
      return tr("Big model unavailable, %1 is driving until the next drive.").arg(defaultModelName("qcom"));
    }
    return tr("Big model unavailable until the next drive.");
  }

  const QString big_name = slotBundleName("chestnut");
  if (big_is_default) {
    return tr("%1 will drive. If it fails during a drive, %2 takes over until the next drive.").arg(big_name, defaultModelName("qcom"));
  }
  return tr("%1 will drive when the chestnut is ready.").arg(big_name);
}

/**
 * @brief (source, name) of what actually drives. Runner-matched, offroad
 * semantics; mirrors carrying_model() in the 2026-02 model_info.py.
 */
QPair<QString, QString> ModelsPanel::carryingModel() {
  const SubMaster &sm = *(uiStateSP()->sm);
  const bool chestnut_present = sm["deviceState"].getDeviceState().getChestnutPresent();
  if (chestnut_present) {
    if (slotActiveRef("chestnut") == DEFAULT_MODEL) {
      // Default big cannot carry offroad until ready; stock modeld runs the Default small
      return {QStringLiteral("qcom"), defaultModelName("qcom")};
    }
    return {QStringLiteral("chestnut"), slotBundleName("chestnut")};
  }
  return {QStringLiteral("qcom"), slotBundleName("qcom")};
}

void ModelsPanel::updateModelManagerState() {
  const SubMaster &sm = *(uiStateSP()->sm);
  model_manager = sm["modelManagerSP"].getModelManagerSP();
}

// Hardware source of the running system: "chestnut" (big model) when the
// chestnut hardware is present and we're offroad, otherwise "qcom" (small).
// Mirrors get_active_source() in sunnypilot/models/helpers.py.
QString ModelsPanel::activeSource() const {
  const SubMaster &sm = *(uiStateSP()->sm);
  const bool chestnut_present = sm["deviceState"].getDeviceState().getChestnutPresent();
  return (chestnut_present && !is_onroad) ? "chestnut" : "qcom";
}

QString ModelsPanel::activeBundleKey(const QString &source) const {
  return (source == "chestnut") ? "ModelManager_ActiveBundleChestnut" : "ModelManager_ActiveBundle";
}

QString ModelsPanel::defaultModelName(const QString &source) const {
  // Matches sunnypilot/models/model_name.py: CD210 (small) / Lebowski (big)
  return (source == "chestnut") ? tr("Lebowski (Default)") : tr("CD210 (Default)");
}

// Bundles for a slot: live list from modelManagerSP for the active source,
// or the manager's cached JSON (ModelManager_ModelsCache[_Chestnut]) otherwise.
QList<ModelsPanel::BundleInfo> ModelsPanel::bundlesForSource(const QString &source) {
  QList<BundleInfo> out;
  const int required_json_version = 18;  // REQUIRED_JSON_VERSION in sunnypilot/models/helpers.py

  if (source == activeSource()) {
    for (const auto &bundle : model_manager.getAvailableBundles()) {
      BundleInfo bi;
      bi.ref = QString::fromStdString(bundle.getRef());
      bi.displayName = QString::fromStdString(bundle.getDisplayName());
      bi.internalName = QString::fromStdString(bundle.getInternalName());
      bi.index = static_cast<int>(bundle.getIndex());
      bi.generation = static_cast<int>(bundle.getGeneration());
      for (const auto &override : bundle.getOverrides()) {
        if (override.getKey() == "folder") {
          bi.folder = QString::fromStdString(override.getValue().cStr());
        }
      }
      out.append(bi);
    }
    return out;
  }

  const std::string cache_key = "ModelManager_ModelsCache" + (source == "chestnut" ? std::string("_Chestnut") : std::string());
  const auto cached = params.get(cache_key);
  if (cached.empty()) {
    return out;
  }

  QJsonParseError parse_error{};
  const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(cached.data(), static_cast<int>(cached.size())), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
    return out;
  }

  const QJsonArray bundles_json = doc.object().value("bundles").toArray();
  for (const auto &entry : bundles_json) {
    const QJsonObject obj = entry.toObject();
    if (obj.value("minimum_selector_version").toInt() != required_json_version) {
      continue;
    }
    BundleInfo bi;
    bi.ref = obj.value("ref").toString();
    bi.displayName = obj.value("display_name").toString();
    bi.internalName = obj.value("short_name").toString();
    bi.index = obj.value("index").toInt();
    bi.generation = obj.value("generation").toInt();
    const QJsonValue overrides = obj.value("overrides");
    if (overrides.isObject()) {
      bi.folder = overrides.toObject().value("folder").toString();
    } else if (overrides.isArray()) {
      for (const auto &ov : overrides.toArray()) {
        const QJsonObject ov_obj = ov.toObject();
        if (ov_obj.value("key").toString() == "folder") {
          bi.folder = ov_obj.value("value").toString();
        }
      }
    }
    out.append(bi);
  }
  return out;
}

// Display name of what's picked in a slot: the stored active bundle's
// displayName, or the source's default. Reads the params slot directly, never
// modelManagerSP.activeBundle (which only reflects the running source).
QString ModelsPanel::slotBundleName(const QString &source) {
  const auto raw = params.get(activeBundleKey(source).toStdString());
  if (!raw.empty()) {
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(raw.data(), static_cast<int>(raw.size())));
    if (doc.isObject()) {
      const QString name = doc.object().value("displayName").toString();
      if (!name.isEmpty()) {
        return name;
      }
    }
  }
  return defaultModelName(source);
}

QString ModelsPanel::slotActiveRef(const QString &source) {
  const auto raw = params.get(activeBundleKey(source).toStdString());
  if (!raw.empty()) {
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(raw.data(), static_cast<int>(raw.size())));
    if (doc.isObject()) {
      const QString ref = doc.object().value("ref").toString();
      if (!ref.isEmpty()) {
        return ref;
      }
    }
  }
  return DEFAULT_MODEL;
}

/**
 * @brief Handles a model slot selection button click ("qcom" small / "chestnut" big).
 * Shows bundles for that source, and stores the pick the same way the 2026-02
 * raylib UI does: "Default" clears the slot's active-bundle key, anything else
 * writes ModelManager_DownloadRef for the manager to resolve.
 */
void ModelsPanel::handleModelSelectClicked(const QString &source) {
  ButtonControlSP *btn = (source == "chestnut") ? bigModelBtn : smallModelBtn;
  btn->setEnabled(false);
  btn->setValue(tr("Fetching models..."));

  const QList<BundleInfo> bundles = bundlesForSource(source);
  if (bundles.isEmpty()) {
    btn->setValue(slotBundleName(source));
    btn->setEnabled(!is_onroad);
    ConfirmationDialog::alert(tr("No models are available for this hardware yet. Connect to the internet and refresh the model list."), this);
    return;
  }

  QList<TreeNode> sortedModels;
  QSet<QString> modelFolders;
  QRegularExpression re("\\(([^)]*)\\)[^(]*$");

  for (const auto &bundle : bundles) {
    modelFolders.insert(bundle.folder);
    sortedModels.append(TreeNode{
      bundle.folder,
      bundle.displayName,
      bundle.ref,
      bundle.index
    });
  }

  std::sort(sortedModels.begin(), sortedModels.end(),
    [](const TreeNode &a, const TreeNode &b) {
      return a.index > b.index;
    });

  // Create a list of folder-maxIndex pairs for sorting
  QList<QPair<QString, int>> folderMaxIndices;
  for (const auto &folder : modelFolders) {
    int maxIndex = -1;
    for (const auto &model : sortedModels) {
      if (model.folder == folder) {
        maxIndex = std::max(maxIndex, model.index);
      }
    }
    folderMaxIndices.append(qMakePair(folder, maxIndex));
  }

  // Sort folders by their highest model index
  std::sort(folderMaxIndices.begin(), folderMaxIndices.end(),
      [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
          return a.second > b.second;
      });

  // Create the final items list using sorted folders
  QList<TreeFolder> items;
  for (const auto &folderPair : folderMaxIndices) {
    QList<TreeNode> folderModels;
    QString folder = folderPair.first;
    for (const auto &model : sortedModels) {
      if (model.folder == folderPair.first) {
        if (model.index == folderPair.second) {
          QRegularExpressionMatch match = re.match(model.displayName);
          if (match.hasMatch()) {
            folder.append(" - (Updated: ").append(match.captured(1)).append(")");
          }
        }
        folderModels.append(model);
      }
    }
    items.append(TreeFolder{folder, folderModels});
  }

  items.insert(0, TreeFolder{"", {
    TreeNode{"", defaultModelName(source), DEFAULT_MODEL, -1}
  }});

  btn->setValue(slotBundleName(source));

  const QString selectedBundleRef = TreeOptionDialog::getSelection(
    tr("Select a Model"), items, slotActiveRef(source), QString("ModelManager_Favs"), this);

  if (selectedBundleRef.isEmpty() || !canContinueOnMeteredDialog()) {
    btn->setEnabled(!is_onroad);
    return;
  }

  if (selectedBundleRef == DEFAULT_MODEL) {
    // "Default" clears the slot, exactly like ACTIVE_BUNDLE_KEYS handling
    params.remove(activeBundleKey(source).toStdString());
  } else {
    params.put("ModelManager_DownloadRef", selectedBundleRef.toStdString());
    // Suggest calibration reset when switching model generations
    for (const auto &bundle : bundles) {
      if (bundle.ref == selectedBundleRef && source == activeSource() &&
          bundle.generation != model_manager.getActiveBundle().getGeneration()) {
        showResetParamsDialog();
        break;
      }
    }
  }

  updateLabels();
}

/**
 * @brief Updates the UI elements based on current state
 */
void ModelsPanel::updateLabels() {
  if (!isVisible()) {
    return;
  }

  updateModelManagerState();
  handleBundleDownloadProgress();

  // Both slots follow the 2026-02 raylib behavior: selectable offroad only.
  // The slot whose pick is actually driving is highlighted green, like
  // _update_state()'s set_value(name, color) in the baseline layout.
  smallModelBtn->setEnabled(!is_onroad && !isDownloading());
  bigModelBtn->setEnabled(!is_onroad);
  const auto [carry_source, carry_name] = carryingModel();
  const QString small_name = slotBundleName("qcom");
  const QString big_name = slotBundleName("chestnut");
  smallModelBtn->setValue(small_name, carry_source == "qcom" && small_name == carry_name ? QString("#33ab4c") : std::optional<QString>{});
  bigModelBtn->setValue(big_name, carry_source == "chestnut" && big_name == carry_name ? QString("#33ab4c") : std::optional<QString>{});

  // Update lagdToggle description with current value
  QString desc = tr("Enable this for the car to learn and adapt its steering response time. "
                   "Disable to use a fixed steering response time. Keeping this on provides the stock openpilot experience.");
  bool lagdEnabled = params.getBool("LagdToggle");
  if (lagdEnabled) {
    auto liveDelayBytes = params.get("LiveDelay");
    if (!liveDelayBytes.empty()) {
      auto LD = loadCerealEvent(params, "LiveDelay");
      float lateralDelay = LD->getLateralDelay().getLateralDelay();
      desc += QString("<br><br><b><span style=\"color:#e0e0e0\">%1</span></b> <span style=\"color:#e0e0e0\">%2 s</span>")
              .arg(tr("Live Steer Delay:")).arg(QString::number(lateralDelay, 'f', 3));
    }
  } else {
    auto carParamsBytes = params.get("CarParamsPersistent");
    if (!carParamsBytes.empty()) {
      AlignedBuffer aligned_buf_cp;
      capnp::FlatArrayMessageReader cmsg(aligned_buf_cp.align(carParamsBytes.data(), carParamsBytes.size()));
      cereal::CarParams::Reader CP = cmsg.getRoot<cereal::CarParams>();

      float steerDelay = CP.getSteerActuatorDelay();
      float softwareDelay = QString::fromStdString(params.get("LagdToggleDelay")).toFloat();
      float totalLag = steerDelay + softwareDelay;
      desc += QString("<br><br><span style=\"color:#e0e0e0\">"
                      "<b>%1</b> %2 s + <b>%3</b> %4 s = <b>%5</b> %6 s</span>")
             .arg(tr("Actuator Delay:"), QString::number(steerDelay, 'f', 2),
                  tr("Software Delay:"), QString::number(softwareDelay, 'f', 2),
                  tr("Total Delay:"), QString::number(totalLag, 'f', 2));
    }
  }
  lagd_toggle_control->setDescription(desc);

  delay_control->setVisible(!params.getBool("LagdToggle") && params.getBool("ShowAdvancedControls"));
  if (delay_control->isVisible()) {
    float value = QString::fromStdString(params.get("LagdToggleDelay")).toFloat();
    delay_control->setLabel(QString::number(value, 'f', 2) + "s");
  }

  // Update lane turn desire label and visibility
  refreshLaneTurnValueControl();
  refreshCameraOffsetControl();
}

/**
 * @brief Shows dialog prompting user to reset calibration after model download
 */
void ModelsPanel::showResetParamsDialog() {
  const auto confirmMsg = QString("%1<br><br><b>%2</b><br><br><b>%3</b>")
                          .arg(tr("Model download has started in the background."))
                          .arg(tr("We STRONGLY suggest you to reset calibration."))
                          .arg(tr("Would you like to do that now?"));
  const auto button_text = tr("Reset Calibration");

  QString content("<body><h2 style=\"text-align: center;\">" + tr("Driving Model Selector") + "</h2><br>"
                  "<p style=\"text-align: center; margin: 0 128px; font-size: 50px;\">" + confirmMsg + "</p></body>");

  if (showConfirmationDialog(content, button_text, false)) {
    params.remove("CalibrationParams");
    params.remove("LiveTorqueParameters");
  }
}

void ModelsPanel::clearModelCache() {
  QString confirmMsg = tr("This will delete ALL downloaded models from the cache"
                            "<br/><u>except the currently active model</u>."
                            "<br/><br/>Are you sure you want to continue?");
  QString content("<body><h2 style=\"text-align: center;\">" + tr("Driving Model Selector") + "</h2><br>"
                "<p style=\"text-align: center; margin: 0 128px; font-size: 50px;\">" + confirmMsg + "</p></body>");
  if (showConfirmationDialog(
    content,
    tr("Clear Cache"))) {
      params.putBool("ModelManager_ClearCache", true);
    }
}

double ModelsPanel::calculateCacheSize() {
  QFuture<qint64> future_ModelCacheSize = QtConcurrent::run([=]() {

    QDir model_dir(QString::fromStdString(Path::model_root()));
    QFileInfoList model_files = model_dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    qint64 totalSize = 0;
    for (const QFileInfo &model_file : model_files) {
        if (model_file.isFile()) {
            totalSize += model_file.size();
        }
    }
    return totalSize;
  });
  return static_cast<double>(future_ModelCacheSize) / (1024.0 * 1024.0);
}

void ModelsPanel::showEvent(QShowEvent *event) {
  lagd_toggle_control->showDescription();
  if (delay_control->isVisible()) {
    delay_control->showDescription();
  }
  refreshCameraOffsetControl();
  if (camera_offset_control->isVisible()) {
    camera_offset_control->showDescription();
  }
}
