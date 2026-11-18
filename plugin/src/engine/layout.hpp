#pragma once

// Hand-measured engine layouts are only trusted after a check against the
// running game. If a class the controller relies on has another size than the
// one its layout was measured on, the feature turns itself off and says why.

namespace tandem::plugin::engine {
// The move component, entity and placed component layouts the puppet
// controller reads (RED4ext.SDK, measured on 2.50).
bool MovementLayoutMatches();
} // namespace tandem::plugin::engine
