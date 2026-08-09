"""acados stub package.

Exposes build paths that SConstruct/SConscripts need to locate the
vendored acados library under third_party/acados/. The actual
acados_template Python package is made importable by adding
third_party/acados/ to submodule_python_paths in SConstruct (so that
`import acados_template` and its internal absolute imports work).

Attributes (used by SConstruct + longitudinal_mpc_lib SConscript):
  DIR           - root of vendored acados tree
  INCLUDE_DIR   - C headers (acados/, blasfeo/include/, hpipm/include/)
  LIB_DIR       - per-arch shared libs (larch64/lib or x86_64/lib)
  TEMPLATE_DIR  - Python codegen templates (Cython .pyx/.pxd + Jinja .in.c)
  TERA_PATH     - t_renderer binary (arch-specific)
"""
import os as _os
import platform as _platform

_HERE = _os.path.dirname(_os.path.abspath(__file__))
_REPO_ROOT = _os.path.abspath(_os.path.join(_HERE, "..", ".."))
_ACADOS_ROOT = _os.path.join(_REPO_ROOT, "third_party", "acados")

_MACHINE = _platform.machine()
if _MACHINE in ("aarch64", "arm64"):
    _ARCH = "larch64"
elif _MACHINE in ("x86_64", "AMD64"):
    _ARCH = "x86_64"
else:
    _ARCH = _MACHINE

DIR = _ACADOS_ROOT
INCLUDE_DIR = _os.path.join(_ACADOS_ROOT, "include")
LIB_DIR = _os.path.join(_ACADOS_ROOT, _ARCH, "lib")
TEMPLATE_DIR = _os.path.join(_ACADOS_ROOT, "acados_template")
TERA_PATH = _os.path.join(_ACADOS_ROOT, _ARCH, "t_renderer")

# Support `from acados.acados_template import ...` by re-exporting the real
# top-level acados_template package (third_party/acados/ must be on
# submodule_python_paths so acados_template is importable at the top level;
# its internal `from acados_template.utils import ...` then resolves).
try:
    import acados_template as _acados_template
    import sys as _sys
    _sys.modules[__name__ + ".acados_template"] = _acados_template
    acados_template = _acados_template
except ImportError:
    # third_party/acados/ not on PYTHONPATH; consumers doing
    # `from acados.acados_template import ...` will fail explicitly.
    pass
