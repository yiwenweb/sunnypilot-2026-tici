#pragma once

#include "openpilot/common/util.h"
#include "openpilot/selfdrive/ui/qt/api.h"

#ifdef SUNNYPILOT
#include "openpilot/selfdrive/ui/sunnypilot/ui.h"
#else
#include "openpilot/selfdrive/ui/ui.h"
#endif

class RequestRepeater : public HttpRequest {
public:
  RequestRepeater(QObject *parent, const QString &requestURL, const QString &cacheKey = "", int period = 0, bool while_onroad=false);

private:
  Params params;
  QTimer *timer;
  QString prevResp;
};
