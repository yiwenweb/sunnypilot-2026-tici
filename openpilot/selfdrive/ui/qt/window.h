#pragma once

#include <QStackedLayout>
#include <QWidget>

#include "openpilot/selfdrive/ui/qt/home.h"
#include "openpilot/selfdrive/ui/qt/offroad/onboarding.h"
#include "openpilot/selfdrive/ui/qt/offroad/settings.h"

#ifdef SUNNYPILOT
#include "openpilot/selfdrive/ui/sunnypilot/ui.h"
#endif

class MainWindow : public QWidget {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = 0) : MainWindow(parent, nullptr, nullptr) {}

protected:
  explicit MainWindow(QWidget *parent, HomeWindow *hw = nullptr, SettingsWindow *sw = nullptr);
  HomeWindow *homeWindow;
  SettingsWindow *settingsWindow;
  virtual void closeSettings();

private:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void openSettings(int index = 0, const QString &param = "");

  QStackedLayout *main_layout;
  OnboardingWindow *onboardingWindow;
};
