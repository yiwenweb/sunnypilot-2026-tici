#pragma once

#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

#include "openpilot/cereal/gen/cpp/log.capnp.h"

// no-op base hw class
class HardwareNone {
public:
  static std::string get_name() { return ""; }
  static cereal::InitData::DeviceType get_device_type() { return cereal::InitData::DeviceType::UNKNOWN; }

  static std::string get_serial() { return "cccccc"; }

  static std::map<std::string, std::string> get_init_logs(bool route_log = false) {
    return {};
  }

  static void set_ir_power(int percentage) {}

  // Defaults so the sunnypilot Qt UI links on any platform; comma hardware
  // overrides these in comma/hardware.h.
  static void reboot() {}
  static void set_display_power(bool on) {}
  static void set_brightness(int percent) {}
  static int get_brightness() { return 0; }

  static bool PC() { return false; }
};
