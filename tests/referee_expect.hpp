#pragma once

// The whole suite runs under the referee (tests/main.cpp,
// docs/architecture/sim.md): every endpoint a test makes is watched, and a
// violation fails the test. A test that breaks rules on purpose (a forged
// sender, an injected fault) names them, before its endpoints talk; hints
// never fail.

#include <initializer_list>
#include <string_view>

namespace tandem::test {
// The current test may break these rules ("ids.unknown-npc"...).
void ExpectReferee(std::initializer_list<std::string_view> rules);
} // namespace tandem::test
