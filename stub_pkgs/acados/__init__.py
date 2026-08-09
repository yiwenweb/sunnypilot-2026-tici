"""acados stub package.

The comma-deps-acados Python distribution is not shipped with sunnypilot-2026.
This stub exposes the paths inside third_party/acados/ so the SConstruct/SConscripts
can locate acados headers, libraries and the acados_template Python package.
"""
import os as _os

_HERE = _os.path.dirname(_os.path.abspath(__file__))
_REPO_ROOT = _os.path.abspath(_os.path.join(_HERE, "..", ".."))
_ACADOS_ROOT = _os.path.join(_REPO_ROOT, "third_party", "acados")

# arch subdirectory (larch64 for C3/tici, x86_64 for dev machines)
import platform as _platform
_MACHINE = _platform.machine()
if _MACHINE in ("aarch64", "arm64"):
    _ARCH = "larch64"
elif _MACHINE in ("x86_64", "AMD64"):
    _ARCH = "x86_64"
else:
    _ARCH = _MACHINE  # fall back

DIR = _ACADOS_ROOT
INCLUDE_DIR = _os.path.join(_ACADOS_ROOT, "include")
LIB_DIR = _os.path.join(_ACADOS_ROOT, _ARCH, "lib")
TEMPLATE_DIR = _os.path.join(_ACADOS_ROOT, "acados_template")
TERA_PATH = _os.path.join(_ACADOS_ROOT, _ARCH, "t_renderer")
