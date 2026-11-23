#pragma once

// Whether the observer (observer.hpp) is on: read by every log line
// (log.hpp) and every hook that feeds the observer. Apart so neither header
// pulls the other (log.hpp brings windows.h through RED4ext's logger).

#include <atomic>
#include <string>

namespace tandem::plugin::log {
inline std::atomic<bool> g_observe{false};
inline bool Observing() { return g_observe.load(std::memory_order_relaxed); }
// A line for the observer (observer.cpp).
void Observe(char level, const std::string &message);
} // namespace tandem::plugin::log
