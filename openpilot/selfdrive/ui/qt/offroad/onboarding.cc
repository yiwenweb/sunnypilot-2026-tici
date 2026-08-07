#include "openpilot/selfdrive/ui/qt/offroad/onboarding.h"

#include <string>

#include <QLabel>
#include <QPainter>
#include <QTransform>
#include <QVBoxLayout>

#include "openpilot/common/util.h"
#include "openpilot/common/params.h"
#include "openpilot/selfdrive/ui/qt/util.h"
#include "openpilot/selfdrive/ui/qt/widgets/input.h"

namespace {
  const std::string SUNNYLINK_CONSENT_VERSION = "1.0";
  const std::string SUNNYLINK_CONSENT_DECLINED = "-1";
}

TrainingGuide::TrainingGuide(QWidget *parent) : QFrame(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
}

void TrainingGuide::mouseReleaseEvent(QMouseEvent *e) {
  if (click_timer.elapsed() < 250) {
    return;
  }
  click_timer.restart();

  auto contains = [this](QRect r, const QPoint &pt) {
    if (image.size() != image_raw_size) {
      QTransform transform;
      transform.translate((width()- image.width()) / 2.0, (height()- image.height()) / 2.0);
      transform.scale(image.width() / (float)image_raw_size.width(), image.height() / (float)image_raw_size.height());
      r= transform.mapRect(r);
    }
    return r.contains(pt);
  };

  if (contains(boundingRect[currentIndex], e->pos())) {
    if (currentIndex == 9) {
      const QRect yes = QRect(707, 804, 531, 164);
      Params().putBool("RecordFront", contains(yes, e->pos()));
    }
    currentIndex += 1;
  } else if (currentIndex == (boundingRect.size() - 2) && contains(boundingRect.last(), e->pos())) {
    currentIndex = 0;
  }

  if (currentIndex >= (boundingRect.size() - 1)) {
    emit completedTraining();
  } else {
    update();
  }
}

void TrainingGuide::showEvent(QShowEvent *event) {
  currentIndex = 0;
  click_timer.start();
}

QImage TrainingGuide::loadImage(int id) {
  QImage img(img_path + QString("step%1.png").arg(id));
  image_raw_size = img.size();
  if (image_raw_size != rect().size()) {
    img = img.scaled(width(), height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  return img;
}

void TrainingGuide::paintEvent(QPaintEvent *event) {
  QPainter painter(this);

  QRect bg(0, 0, painter.device()->width(), painter.device()->height());
  painter.fillRect(bg, QColor("#000000"));

  image = loadImage(currentIndex);
  QRect rect(image.rect());
  rect.moveCenter(bg.center());
  painter.drawImage(rect.topLeft(), image);

  // progress bar
  if (currentIndex > 0 && currentIndex < (boundingRect.size() - 2)) {
    const int h = 20;
    const int w = (currentIndex / (float)(boundingRect.size() - 2)) * width();
    painter.fillRect(QRect(0, height() - h, w, h), QColor("#465BEA"));
  }
}

void TermsPage::showEvent(QShowEvent *event) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(45, 35, 45, 45);
  main_layout->setSpacing(0);

  QVBoxLayout *vlayout = new QVBoxLayout();
  vlayout->setContentsMargins(165, 165, 165, 0);
  main_layout->addLayout(vlayout);

  QLabel *title = new QLabel(tr("Welcome to sunnypilot"));
  title->setStyleSheet("font-size: 90px; font-weight: 500;");
  vlayout->addWidget(title, 0, Qt::AlignTop | Qt::AlignLeft);

  vlayout->addSpacing(90);
  QLabel *desc = new QLabel(tr("You must accept the Terms and Conditions to use sunnypilot. Read the latest terms at <span style='color: #465BEA;'>https://comma.ai/terms</span> before continuing."));
  desc->setWordWrap(true);
  desc->setStyleSheet("font-size: 80px; font-weight: 300;");
  vlayout->addWidget(desc, 0);

  vlayout->addStretch();

  QHBoxLayout* buttons = new QHBoxLayout;
  buttons->setMargin(0);
  buttons->setSpacing(45);
  main_layout->addLayout(buttons);

  QPushButton *decline_btn = new QPushButton(tr("Decline"));
  buttons->addWidget(decline_btn);
  QObject::connect(decline_btn, &QPushButton::clicked, this, &TermsPage::declinedTerms);

  accept_btn = new QPushButton(tr("Agree"));
  accept_btn->setStyleSheet(R"(
    QPushButton {
      background-color: #465BEA;
    }
    QPushButton:pressed {
      background-color: #3049F4;
    }
  )");
  buttons->addWidget(accept_btn);
  QObject::connect(accept_btn, &QPushButton::clicked, this, &TermsPage::acceptedTerms);
}

void DeclinePage::showEvent(QShowEvent *event) {
  if (layout()) {
    return;
  }

  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setMargin(45);
  main_layout->setSpacing(40);

  QLabel *text = new QLabel(this);
  text->setText(tr("You must accept the Terms and Conditions in order to use sunnypilot."));
  text->setStyleSheet(R"(font-size: 80px; font-weight: 300; margin: 200px;)");
  text->setWordWrap(true);
  main_layout->addWidget(text, 0, Qt::AlignCenter);

  QHBoxLayout* buttons = new QHBoxLayout;
  buttons->setSpacing(45);
  main_layout->addLayout(buttons);

  QPushButton *back_btn = new QPushButton(tr("Back"));
  buttons->addWidget(back_btn);

  QObject::connect(back_btn, &QPushButton::clicked, this, &DeclinePage::getBack);

  QPushButton *uninstall_btn = new QPushButton(tr("Decline, uninstall %1").arg(getBrand()));
  uninstall_btn->setStyleSheet("background-color: #B73D3D");
  buttons->addWidget(uninstall_btn);
  QObject::connect(uninstall_btn, &QPushButton::clicked, [=]() {
    Params().putBool("DoUninstall", true);
  });
}

SunnylinkConsentPage::SunnylinkConsentPage(QWidget *parent) : QFrame(parent) {}

void SunnylinkConsentPage::showEvent(QShowEvent *event) {
  if (layout()) {
    updateUI();
    return;
  }

  current_step = 0;

  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(45, 35, 45, 45);
  main_layout->setSpacing(0);

  QVBoxLayout *vlayout = new QVBoxLayout();
  vlayout->setContentsMargins(165, 165, 165, 0);
  main_layout->addLayout(vlayout);

  QLabel *title = new QLabel(tr("sunnylink"));
  title->setStyleSheet("font-size: 90px; font-weight: 500;");
  vlayout->addWidget(title, 0, Qt::AlignTop | Qt::AlignLeft);

  vlayout->addSpacing(90);
  desc_label = new QLabel();
  desc_label->setWordWrap(true);
  desc_label->setStyleSheet("font-size: 80px; font-weight: 300;");
  vlayout->addWidget(desc_label, 0);

  vlayout->addStretch();

  QHBoxLayout* buttons = new QHBoxLayout;
  buttons->setMargin(0);
  buttons->setSpacing(45);
  main_layout->addLayout(buttons);

  secondary_btn = new QPushButton();
  buttons->addWidget(secondary_btn);
  QObject::connect(secondary_btn, &QPushButton::clicked, [=]() {
    current_step = current_step == 0 ? 1 : 0;
    updateUI();
  });

  primary_btn = new QPushButton(tr("Enable"));
  primary_btn->setStyleSheet(R"(
    QPushButton {
      background-color: #465BEA;
    }
    QPushButton:pressed {
      background-color: #3049F4;
    }
  )");
  buttons->addWidget(primary_btn);
  QObject::connect(primary_btn, &QPushButton::clicked, [=]() {
    Params().putBool("SunnylinkEnabled", true);
    Params().put("CompletedSunnylinkConsentVersion", SUNNYLINK_CONSENT_VERSION);
    emit completedConsent();
  });

  danger_btn = new QPushButton(tr("Disable"));
  danger_btn->setStyleSheet("background-color: #B73D3D;");
  buttons->addWidget(danger_btn);
  QObject::connect(danger_btn, &QPushButton::clicked, [=]() {
    Params().putBool("SunnylinkEnabled", false);
    Params().put("CompletedSunnylinkConsentVersion", SUNNYLINK_CONSENT_DECLINED);
    emit completedConsent();
  });

  updateUI();
}

void SunnylinkConsentPage::updateUI() {
  if (current_step == 0) {
    desc_label->setText(tr("sunnylink enables secured remote access to your comma device from anywhere, "
                           "including settings management, remote monitoring, real-time dashboard, etc."));
    secondary_btn->setText(tr("Disable"));
    primary_btn->setVisible(true);
    danger_btn->setVisible(false);
  } else {
    desc_label->setText(tr("sunnylink is designed to be enabled as part of sunnypilot's core functionality. "
                           "If sunnylink is disabled, features such as settings management, remote monitoring, "
                           "real-time dashboards will be unavailable."));
    secondary_btn->setText(tr("Back"));
    primary_btn->setVisible(false);
    danger_btn->setVisible(true);
  }
}


void OnboardingWindow::updateActiveScreen() {
  if (!accepted_terms) {
    setCurrentIndex(0);
  } else if (!training_done) {
    setCurrentIndex(1);
  } else if (!sunnylink_consent_done) {
    setCurrentIndex(3);
  } else {
    emit onboardingDone();
  }
}

OnboardingWindow::OnboardingWindow(QWidget *parent) : QStackedWidget(parent) {
  std::string current_terms_version = params.get("TermsVersion");
  std::string current_training_version = params.get("TrainingVersion");
  accepted_terms = params.get("HasAcceptedTerms") == current_terms_version;
  training_done = params.get("CompletedTrainingVersion") == current_training_version;
  std::string consent_version = params.get("CompletedSunnylinkConsentVersion");
  sunnylink_consent_done = consent_version == SUNNYLINK_CONSENT_VERSION ||
                           consent_version == SUNNYLINK_CONSENT_DECLINED;

  TermsPage* terms = new TermsPage(this);
  addWidget(terms);
  connect(terms, &TermsPage::acceptedTerms, [=]() {
    params.put("HasAcceptedTerms", current_terms_version);
    accepted_terms = true;
    updateActiveScreen();
  });
  connect(terms, &TermsPage::declinedTerms, [=]() { setCurrentIndex(2); });

  TrainingGuide* tr = new TrainingGuide(this);
  addWidget(tr);
  connect(tr, &TrainingGuide::completedTraining, [=]() {
    training_done = true;
    params.put("CompletedTrainingVersion", current_training_version);
    updateActiveScreen();
  });

  DeclinePage* declinePage = new DeclinePage(this);
  addWidget(declinePage);
  connect(declinePage, &DeclinePage::getBack, [=]() { updateActiveScreen(); });

  SunnylinkConsentPage* consentPage = new SunnylinkConsentPage(this);
  addWidget(consentPage);
  connect(consentPage, &SunnylinkConsentPage::completedConsent, [=]() {
    sunnylink_consent_done = true;
    updateActiveScreen();
  });

  setStyleSheet(R"(
    * {
      color: white;
      background-color: black;
    }
    QPushButton {
      height: 160px;
      font-size: 55px;
      font-weight: 400;
      border-radius: 10px;
      background-color: #4F4F4F;
    }
  )");
  updateActiveScreen();
}
