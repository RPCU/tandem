#pragma once

// How much slower than an idle machine this one runs the tests: every wait
// for a state ("until the guest is joined, at most 3 s") is stretched by
// it, so a test stays strict on what must happen and tolerant of a busy
// machine (a build beside it, three test runs at once). Fixed windows that
// check something does not happen are left as they are.
//
// TANDEM_TEST_SLOWDOWN=F sets it (1 to 20); otherwise, and at least, the
// machine's load per core at each wait (/proc/loadavg), from 1.
// docs/architecture/testing.md, "Busy machines".

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

namespace tandem::test {
inline double Slowdown() {
  static const double asked = [] {
    if (const char *text = std::getenv("TANDEM_TEST_SLOWDOWN")) {
      char *end = nullptr;
      const double value = std::strtod(text, &end);
      if (end != text && value > 0.0)
        return value;
    }
    return 1.0;
  }();
  // The load now (each wait reads it: a build next door starts and stops).
  double out = asked;
  double load = 0.0;
  if (std::ifstream loadavg("/proc/loadavg"); loadavg >> load) {
    const double cores = std::max(1u, std::thread::hardware_concurrency());
    out = std::max(out, load / cores);
  }
  return std::clamp(out, 1.0, 20.0);
}

// An upper bound on a wait for a state, stretched by Slowdown(). Waits end
// as soon as the state is reached: a longer bound costs nothing then.
template <class Rep, class Period>
std::chrono::milliseconds Patience(std::chrono::duration<Rep, Period> limit) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(limit * Slowdown());
}
} // namespace tandem::test
