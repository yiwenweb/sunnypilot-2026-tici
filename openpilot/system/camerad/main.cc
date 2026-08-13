#include "system/camerad/cameras/camera_common.h"

#include <cassert>

#include "common/params.h"
#include "common/util.h"

int main(int argc, char *argv[]) {
  // doesn't need RT priority since we're using isolcpus
  int ret = util::set_core_affinity({6});
  if (ret != 0) {
    LOGW("camerad failed to set core affinity to core 6: %d", ret);
  }

  camerad_thread();
  return 0;
}
