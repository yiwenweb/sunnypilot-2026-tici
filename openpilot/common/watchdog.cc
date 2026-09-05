#include <string>
#include <unistd.h>

#include "openpilot/common/watchdog.h"
#include "openpilot/common/util.h"
#include "openpilot/common/hardware/hw.h"

const std::string watchdog_fn_prefix = Path::shm_path() + "/wd_";  // + <pid>

bool watchdog_kick(uint64_t ts) {
  static std::string fn = watchdog_fn_prefix + std::to_string(getpid());
  return util::write_file(fn.c_str(), &ts, sizeof(ts), O_WRONLY | O_CREAT) > 0;
}
