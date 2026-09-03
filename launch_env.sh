#!/usr/bin/env bash

export OMP_NUM_THREADS=1
export MKL_NUM_THREADS=1
export NUMEXPR_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1
export VECLIB_MAXIMUM_THREADS=1

# models get lower priority than ui
# - ui is ~5ms
# - modeld is 20ms
# - DM is 10ms
# in order to run ui at 60fps (16.67ms), we need to allow
# it to preempt the model workloads. we have enough
# headroom for this until ui is moved to the CPU.
export QCOM_PRIORITY=12

if [ -z "$AGNOS_VERSION" ]; then
  export AGNOS_VERSION="13.1"  # C3 compatible version (19.6 is for C3X only)
fi

export STAGING_ROOT="/data/safe_staging"

# --- tici (comma three) specifics -------------------------------------------
# The weston unit keeps its socket in /var/tmp/weston, not in the default
# /run/user/<uid>. raylib resolves $XDG_RUNTIME_DIR/wayland-0, so without this
# the UI dies with "Failed to create a Wayland display / Failed to initialize EGL".
if [ -d /var/tmp/weston ]; then
  export XDG_RUNTIME_DIR="/var/tmp/weston"
  # weston runs as root and creates the socket 0755, which leaves the comma user
  # (i.e. openpilot) unable to connect to it.
  if [ -S /var/tmp/weston/wayland-0 ]; then
    sudo chmod 777 /var/tmp/weston/wayland-0 2>/dev/null || true
  fi
fi

# core_ctl hotplugs the big cores (4-7) off while the device is idle. Any daemon
# pinned to one of them then dies on sched_setaffinity(EINVAL) - card, controlsd,
# modeld, dmonitoringmodeld ... all showed up as "process not running".
# set_core_affinity() now degrades gracefully, but bring them back up anyway so
# the real-time daemons actually get their core.
for cpu in 4 5 6 7; do
  if [ -e "/sys/devices/system/cpu/cpu${cpu}/online" ]; then
    echo 1 | sudo tee "/sys/devices/system/cpu/cpu${cpu}/online" > /dev/null 2>&1 || true
  fi
done
