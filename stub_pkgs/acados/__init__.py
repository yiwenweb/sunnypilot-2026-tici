"""acados stub package.

The comma-deps-acados Python distribution is not shipped with sunnypilot-2026.
This stub exposes:

1. Build paths (DIR / INCLUDE_DIR / LIB_DIR / TEMPLATE_DIR / TERA_PATH) so
   SConstruct/SConscripts can locate headers, per-arch libs, and t_renderer.
2. A ``acados.acados_template`` submodule aliased to
   ``third_party/acados/acados_template`` so that code like
   ``from acados.acados_template import AcadosOcp`` works during code
   generation (long_mpc.py).
"""
import os as _os
import sys as _sys
import importlib as _importlib
import importlib.util as _importlib_util

_HERE = _os.path.dirname(_os.path.abspath(__file__))
_REPO_ROOT = _os.path.abspath(_os.path.join(_HERE, "..", ".."))
_ACADOS_ROOT = _os.path.join(_REPO_ROOT, "third_party", "acados")

# Pick the right arch subdirectory (larch64 for C3/tici, x86_64 for dev).
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

# Expose third_party/acados/acados_template as `acados.acados_template`
# so that ``from acados.acados_template import ...`` succeeds.
_pkg_init = _os.path.join(TEMPLATE_DIR, "__init__.py")
if _os.path.isfile(_pkg_init):
    __path__.append(TEMPLATE_DIR)  # allow `acados.<name>` to look there too
    # Force-load the acados_template package under the name acados.acados_template.
    _spec = _importlib_util.spec_from_file_location(
        "acados.acados_template", _pkg_init,
        submodule_search_locations=[TEMPLATE_DIR],
    )
    if _spec is not None:
        _mod = _importlib_util.module_from_spec(_spec)
        _sys.modules["acados.acados_template"] = _mod
        _spec.loader.exec_module(_mod)
