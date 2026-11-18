// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "layout.hpp"

#include <RED4ext/RTTISystem.hpp>

namespace tandem::plugin::engine {
namespace {
bool SizeIs(const char *name, std::uint32_t expected) {
  auto *rtti = RED4ext::CRTTISystem::Get();
  auto *type = rtti ? rtti->GetClass(name) : nullptr;
  if (!type) {
    log::Error("layout check: class {} not found", name);
    return false;
  }
  if (type->size != expected) {
    log::Error("layout check: {} is 0x{:X} bytes, measured as 0x{:X} on 2.50", name, type->size,
               expected);
    return false;
  }
  return true;
}
} // namespace

bool MovementLayoutMatches() {
  // Evaluate all of them so the log names every mismatch at once.
  bool ok = SizeIs("moveComponent", 0x2C0);
  ok = SizeIs("entEntity", 0x160) && ok;
  ok = SizeIs("entIPlacedComponent", 0x120) && ok;
  ok = SizeIs("entIComponent", 0x90) && ok;
  // Not read by the controller, but the 2.5 build fingerprint NCO found.
  ok = SizeIs("gameuiCharacterCustomizationSystem", 0x3D8) && ok;
  return ok;
}
} // namespace tandem::plugin::engine
