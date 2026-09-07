#!/usr/bin/env python3
"""fill_version_params.py - keep Updater* version params populated while
the official `updated` process is disabled.

Why this exists
---------------
Most Updater* params are flagged CLEAR_ON_MANAGER_START in
openpilot/common/params_keys.h, i.e. manager wipes them on every launch.
Normally the `updated` process repopulates them; we keep `updated` disabled
(on purpose, to stop it running `git clean` and wiping the custom build).
Without updated, the Settings > Software page then shows blank version info.

This daemon is pure userspace: every INTERVAL seconds it derives the real
version from git + sunnypilot version.h and refills the params whenever they
are missing or the checked-out commit changed. It never fetches/modifies git.

Usage:
  python3 fill_version_params.py          # daemon loop (launched at boot)
  python3 fill_version_params.py --once   # fill once and exit (manual fix)
"""
import datetime
import os
import subprocess
import sys
import time

BASEDIR = os.getenv("OPENPILOT_BASEDIR", "/data/openpilot")
INTERVAL = float(os.getenv("FILL_VERSION_INTERVAL", "15"))

sys.path.insert(0, BASEDIR)


def run_git(*args: str) -> str:
  return subprocess.check_output(["git", *args], cwd=BASEDIR,
                                 stderr=subprocess.DEVNULL).decode().strip()


def collect_description() -> str:
  """Mirror updated.get_description(): 'version / branch / commit / Mon DD'."""
  try:
    branch = run_git("rev-parse", "--abbrev-ref", "HEAD")
    commit = run_git("rev-parse", "HEAD")[:7]
    version_h = os.path.join(BASEDIR, "openpilot", "sunnypilot", "common", "version.h")
    with open(version_h) as f:
      version = f.read().split('"')[1]
    ts = int(run_git("show", "-s", "--format=%ct", "HEAD"))
    date = datetime.datetime.fromtimestamp(ts).strftime("%b %d")
    return f"{version} / {branch} / {commit} / {date}", branch
  except Exception as e:
    print(f"[fill_version] collect failed: {e}", flush=True)
    return "", ""


def fill_once(params) -> bool:
  desc, branch = collect_description()
  if not desc:
    return False

  changed = False
  cur = params.get("UpdaterCurrentDescription") or ""
  if cur != desc:
    # STRING: software page version/branch rows
    params.put("UpdaterCurrentDescription", desc, block=True)
    # BYTES release notes (empty is fine)
    params.put("UpdaterCurrentReleaseNotes", b"", block=True)
    changed = True

  if not (params.get("UpdaterState") or ""):
    params.put("UpdaterState", "idle", block=True)
    changed = True

  if not (params.get("UpdaterTargetBranch") or "") and branch:
    params.put("UpdaterTargetBranch", branch, block=True)
    changed = True

  # BOOL: never offer a download / install (updated disabled, nothing staged)
  if params.get_bool("UpdaterFetchAvailable"):
    params.put_bool("UpdaterFetchAvailable", False, block=True)
    changed = True
  if params.get_bool("UpdateAvailable"):
    params.put_bool("UpdateAvailable", False, block=True)
    changed = True

  # INT: clear any stale failure counter
  try:
    failed = int(params.get("UpdateFailedCount") or 0)
  except (TypeError, ValueError):
    failed = 0
  if failed > 0:
    params.put("UpdateFailedCount", 0, block=True)
    changed = True

  if changed:
    # TIME type needs a timezone-naive datetime (UTC), same as updated
    now_naive = datetime.datetime.now(datetime.UTC).replace(tzinfo=None)
    params.put("LastUpdateTime", now_naive, block=True)
    print(f"[fill_version] refilled: {desc}", flush=True)
  return changed


def main() -> None:
  from openpilot.common.params import Params
  once = "--once" in sys.argv
  params = Params()
  if once:
    fill_once(params)
    print("[fill_version] --once done", flush=True)
    return
  print(f"[fill_version] daemon started, interval={INTERVAL}s, basedir={BASEDIR}", flush=True)
  while True:
    try:
      fill_once(params)
    except Exception as e:
      print(f"[fill_version] loop error: {e}", flush=True)
    time.sleep(INTERVAL)


if __name__ == "__main__":
  main()
