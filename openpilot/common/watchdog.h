#pragma once

#include <cstdint>

#ifdef __x86_64__
inline bool watchdog_kick(uint64_t ts) { return true; }
#else
bool watchdog_kick(uint64_t ts);
#endif
