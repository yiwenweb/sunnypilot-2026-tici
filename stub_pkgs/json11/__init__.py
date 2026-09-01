import os as _os

# Point to stub_pkgs/ (parent) so `#include "json11/json11.hpp"` resolves
# to stub_pkgs/json11/json11.hpp when comma-deps-json11 is unavailable.
_HERE = _os.path.dirname(_os.path.abspath(__file__))
_STUB_DIR = _os.path.abspath(_os.path.join(_HERE, ".."))
INCLUDE_DIR = _STUB_DIR          # so <json11/json11.hpp> works
LIB_DIR = _HERE                  # scons will build libjson11.a here
