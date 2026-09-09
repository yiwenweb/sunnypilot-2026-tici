/**
 * Copyright (c) 2026, yiwenweb, sunnypilot BYD adaptation.
 *
 * Lane-center correction + auto camera-offset status HUD (Qt port of the
 * raylib LaneCenterStatusRenderer in selfdrive/ui/sunnypilot/onroad/lane_center_status.py).
 *
 * Shows, anchored to the driver-monitoring circle:
 *  - LaneCenterCorrection state: green dot + "居中修正 · 偏右 8cm" while the
 *    correction loop is actively pulling the car back to lane center; grey
 *    "居中修正 · 居中" when armed but centered; "居中修正 · 无车道线" when no
 *    confident lines (mirrors controlsd gating: |offset| > 3cm dead zone).
 *  - CameraOffset state: current value + learning indicator, e.g.
 *    "相机偏移 +0.08m · 学习中" (blue) vs "相机偏移 +0.08m" (green, settled);
 *    manual (AutoCameraOffset off) shows grey "相机偏移 +0.08m（手动）".
 *
 * Data: lane geometry from modelV2 (same lane_center_offset convention as
 * controlsd: y positive = left, negative = right), params read throttled at 2 Hz.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/onroad/lane_center_status.h"

#include <cmath>

#include "openpilot/common/util.h"
#include "openpilot/selfdrive/ui/qt/onroad/buttons.h"
#include "openpilot/selfdrive/ui/qt/util.h"

namespace {

constexpr float DEAD_ZONE = 0.03f;  // same as controlsd lane center correction dead zone (m)

// ---- palette (matches the raylib HUD) ----
const QColor COLOR_ACTIVE(0x2e, 0xcc, 0x71);   // green: correcting / settled
const QColor COLOR_IDLE(0x91, 0x9b, 0x95);     // grey: armed, centered
const QColor COLOR_LEARN(0x4a, 0xa8, 0xea);    // blue: learning
const QColor COLOR_SHADOW(0, 0, 0, 160);
const QColor COLOR_TEXT(255, 255, 255, 220);
constexpr int SHADOW_OFFSET = 2;

constexpr int FONT_SIZE = 40;
constexpr int ROW_PAD = 16;
constexpr int BOX_PAD = 11;
constexpr int GAP = 22;
constexpr float DOT_R = 8.0f;
constexpr int PAD = 16;

struct LcRow {
  QColor dot_color;
  QString title;
  QString value;
  QColor value_color;
};

}  // namespace

void LaneCenterStatus::updateState(const UIState &s) {
  auto &sm = *(s.sm);
  if (sm.rcv_frame("carState") < s.scene.started_frame) {
    return;
  }

  // Params read throttled at 2 Hz (matches the raylib HUD).
  const double now = millis_since_boot() / 1000.0;
  if (now - params_t >= 0.5) {
    params_t = now;
    lc_enabled = params.getBool("LaneCenterCorrection");
    aco_enabled = params.getBool("AutoCameraOffset");
    cam_offset = QString::fromStdString(params.get("CameraOffset")).toFloat();
    learned = QString::fromStdString(params.get("AutoCamOffsetLearned")).toFloat();
  }

  if (sm.rcv_frame("modelV2") < s.scene.started_frame) {
    lines_ok = false;
    correcting = false;
    return;
  }

  is_rhd = sm["driverMonitoringState"].getDriverMonitoringState().getIsRHD();

  const auto mv = sm["modelV2"].getModelV2();
  const auto lane_lines = mv.getLaneLines();
  const auto probs = mv.getLaneLineProbs();
  if (lane_lines.size() >= 3 && probs.size() >= 3 &&
      std::min(probs[1], probs[2]) > 0.5 &&
      lane_lines[1].getY().size() > 0 && lane_lines[2].getY().size() > 0) {
    offset = (lane_lines[1].getY()[0] + lane_lines[2].getY()[0]) / 2.0f;
    lines_ok = true;
  } else {
    lines_ok = false;
  }

  correcting = lc_enabled && lines_ok && std::abs(offset) > DEAD_ZONE;
  // "学习中" = residual not yet compensated (learned not converged to 0).
  learning = aco_enabled && std::abs(learned) > 0.005f;
}

void LaneCenterStatus::draw(QPainter &painter, const QRect &surface_rect) {
  if (!(lc_enabled || aco_enabled)) {
    return;
  }

  QList<LcRow> rows;
  if (lc_enabled) {
    if (correcting) {
      const QString direction = (offset < 0) ? QObject::tr("偏右") : QObject::tr("偏左");
      rows.append({COLOR_ACTIVE, QObject::tr("居中修正"),
                   QString("%1 %2cm").arg(direction).arg((int)std::round(std::abs(offset) * 100)),
                   COLOR_ACTIVE});
    } else if (lines_ok) {
      rows.append({COLOR_IDLE, QObject::tr("居中修正"), QObject::tr("居中"), COLOR_IDLE});
    } else {
      rows.append({COLOR_IDLE, QObject::tr("居中修正"), QObject::tr("无车道线"), COLOR_IDLE});
    }
  }

  if (aco_enabled) {
    if (std::abs(cam_offset) > 0.001f) {
      const QColor color = learning ? COLOR_LEARN : COLOR_ACTIVE;
      const QString suffix = learning ? QObject::tr(" · 学习中") : QString();
      rows.append({color, QObject::tr("相机偏移"),
                   QString("%1%2m%3").arg(cam_offset >= 0 ? "+" : "").arg(QString::number(cam_offset, 'f', 2), suffix),
                   color});
    } else {
      rows.append({COLOR_LEARN, QObject::tr("相机偏移"), QObject::tr("收集中…"), COLOR_LEARN});
    }
  } else if (std::abs(cam_offset) > 0.001f) {
    rows.append({COLOR_IDLE, QObject::tr("相机偏移"),
                 QString("%1%2m%3").arg(cam_offset >= 0 ? "+" : "", QString::number(cam_offset, 'f', 2), QObject::tr("（手动）")),
                 COLOR_IDLE});
  }

  if (rows.isEmpty()) {
    return;
  }

  painter.save();

  const QFont title_font = InterFont(FONT_SIZE, QFont::DemiBold);
  const QFont value_font = InterFont(FONT_SIZE, QFont::Normal);

  const int row_h = FONT_SIZE + ROW_PAD;
  int box_w = 0;
  for (const auto &row : rows) {
    painter.setFont(title_font);
    const int title_w = painter.fontMetrics().horizontalAdvance(row.title);
    painter.setFont(value_font);
    const int value_w = painter.fontMetrics().horizontalAdvance(row.value);
    box_w = std::max(box_w, (int)(DOT_R * 2) + PAD + title_w + PAD + value_w);
  }

  const int box_h = row_h * rows.size() + 2 * BOX_PAD;

  // Position: anchored to the driver-monitoring circle (bottom-left for LHD,
  // bottom-right for RHD), placed on its outside edge and vertically centered.
  const int dm_offset = UI_BORDER_SIZE + btn_size / 2;
  const float btn_cx = surface_rect.width() - (is_rhd ? dm_offset : -dm_offset);
  const float btn_cy = surface_rect.height() - dm_offset;
  float box_x = is_rhd ? (btn_cx - btn_size / 2.0f - GAP - box_w)
                       : (btn_cx + btn_size / 2.0f + GAP);
  const float box_y = btn_cy - box_h / 2.0f;
  box_x = std::max(box_x, (float)surface_rect.x() + 10.0f);

  // No background fill - transparent, the camera view shows through.
  float y = box_y + BOX_PAD;
  for (const auto &row : rows) {
    painter.setFont(title_font);
    const int title_w = painter.fontMetrics().horizontalAdvance(row.title);
    const float tx = box_x + DOT_R * 2 + PAD;
    const float vx = tx + title_w + PAD;
    const float dot_cy = y + FONT_SIZE / 2.0f - 4;

    // drop shadow keeps the text readable on the camera view
    painter.setPen(COLOR_SHADOW);
    painter.setBrush(COLOR_SHADOW);
    painter.drawEllipse(QPointF(box_x + DOT_R + SHADOW_OFFSET, dot_cy + SHADOW_OFFSET), DOT_R, DOT_R);
    painter.drawText(QPointF(tx + SHADOW_OFFSET, y + SHADOW_OFFSET + FONT_SIZE - 6), row.title);
    painter.setFont(value_font);
    painter.drawText(QPointF(vx + SHADOW_OFFSET, y + SHADOW_OFFSET + FONT_SIZE - 6), row.value);

    painter.setPen(row.dot_color);
    painter.setBrush(row.dot_color);
    painter.drawEllipse(QPointF(box_x + DOT_R, dot_cy), DOT_R, DOT_R);
    painter.setFont(title_font);
    painter.setPen(COLOR_TEXT);
    painter.drawText(QPointF(tx, y + FONT_SIZE - 6), row.title);
    painter.setFont(value_font);
    painter.setPen(row.value_color);
    painter.drawText(QPointF(vx, y + FONT_SIZE - 6), row.value);

    y += row_h;
  }

  painter.restore();
}
