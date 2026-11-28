// Engine access that fails soft: see access.hpp.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../degradations.hpp"
#include "../log.hpp"
#include "access.hpp"
#include "animation.hpp"
#include "crowd.hpp"
#include "guards.hpp"
#include "hooks.hpp"
#include "look.hpp"
#include "mappins.hpp"
#include "physics.hpp"
#include "posestream.hpp"
#include "prefabs.hpp"
#include "quests.hpp"
#include "ragdoll.hpp"
#include "slots.hpp"
#include "vehiclemove.hpp"
#include "workspots.hpp"

#include <RED4ext/RTTISystem.hpp>

#include <windows.h>

#include <atomic>
#include <format>
#include <mutex>
#include <vector>

namespace tandem::plugin::engine {
namespace {
enum class State { On, Off, NotAsked };
struct Entry {
  std::string name;
  State state;
  std::string why;
};
// In the order first heard of: the summary's order.
std::mutex g_lock;
std::vector<Entry> g_features;
std::atomic_bool g_checked{false};

// The summary's order (a feature not listed here comes after these).
constexpr std::string_view kOrder[] = {
    feature::kMovement,
    feature::kClipLengths,
    feature::kLooks,
    feature::kLookKeys,
    feature::kThirdPerson,
    feature::kPoseStream,
    feature::kQuestNodes,
    feature::kQuestFreeze,
    feature::kPrefabs,
    feature::kWorkspots,
    feature::kPhysics,
    feature::kTransformGuard,
    feature::kDismembermentGuard,
    feature::kSlideGuard,
    feature::kDisposeGate,
    feature::kTrace,
    feature::kVehicleMove,
    feature::kBodySlots,
    feature::kCrowdSpawners,
    feature::kActivityMarks,
};

// Under g_lock: the entries in the summary's order.
std::vector<const Entry *> Ordered() {
  std::vector<const Entry *> out;
  for (const auto name : kOrder)
    for (const auto &entry : g_features)
      if (entry.name == name)
        out.push_back(&entry);
  for (const auto &entry : g_features) {
    bool listed = false;
    for (const auto name : kOrder)
      listed = listed || entry.name == name;
    if (!listed)
      out.push_back(&entry);
  }
  return out;
}

// Under g_lock.
Entry *EntryOf(std::string_view feature) {
  for (auto &entry : g_features)
    if (entry.name == feature)
      return &entry;
  return nullptr;
}

RED4ext::CClass *ClassOf(const char *name) {
  auto *rtti = RED4ext::CRTTISystem::Get();
  return rtti ? rtti->GetClass(name) : nullptr;
}

// The byte MarkThirdPerson writes (natives.cpp): gamePuppetPS +0x72, not a
// property. Measured on 2.50: the class is 0x78 bytes and the byte sits
// between the properties at +0x71 and +0x74.
void CheckThirdPerson() {
  bool ok = ClassSizeIs(feature::kThirdPerson, "gamePuppetPS", 0x78);
  ok = PropertyAt(feature::kThirdPerson, "gamePuppetPS", "hasQuickHackBegunUpload", 0x71) && ok;
  ok = PropertyAt(feature::kThirdPerson, "gamePuppetPS", "hasAlternativeName", 0x74) && ok;
  auto *object = ClassOf("gameObject");
  if (!object || !object->GetProperty("persistentState")) {
    FeatureOff(feature::kThirdPerson, "gameObject has no persistentState property");
    ok = false;
  }
  if (ok)
    FeatureOn(feature::kThirdPerson);
}
} // namespace

std::uintptr_t Resolve(std::uint32_t hash) {
  using Resolver = std::uintptr_t (*)(std::uint32_t);
  static const Resolver s_resolve = [] {
    const auto module = GetModuleHandleW(L"RED4ext.dll");
    return module ? reinterpret_cast<Resolver>(GetProcAddress(module, "RED4ext_ResolveAddress"))
                  : nullptr;
  }();
  return s_resolve ? s_resolve(hash) : 0;
}

std::uintptr_t FindAddress(std::string_view feature, const char *what, std::uint32_t hash) {
  const auto address = Resolve(hash);
  if (!address)
    FeatureOff(feature,
               std::format("{} (hash {}) is not in this game's address library", what, hash));
  return address;
}

void FeatureOn(std::string_view feature) {
  std::lock_guard _(g_lock);
  if (!EntryOf(feature))
    g_features.push_back({std::string(feature), State::On, {}});
}

void FeatureOff(std::string_view feature, std::string_view reason) {
  {
    std::lock_guard _(g_lock);
    auto *entry = EntryOf(feature);
    if (entry && entry->state == State::Off)
      return;
    if (entry)
      *entry = {std::string(feature), State::Off, std::string(reason)};
    else
      g_features.push_back({std::string(feature), State::Off, std::string(reason)});
  }
  log::Error("engine: {} is off: {}", feature, reason);
  // Never silently (degradations.hpp): the player and the others are told,
  // and a core feature off means no session.
  Degradations::Get().Start(feature, reason);
}

void FeatureNotAsked(std::string_view feature, std::string_view why) {
  std::lock_guard _(g_lock);
  if (!EntryOf(feature))
    g_features.push_back({std::string(feature), State::NotAsked, std::string(why)});
}

void FeatureSwitchedOff(std::string_view feature, std::string_view why) {
  FeatureNotAsked(feature, why);
  // Unless it is off for a reason nobody chose (kept: Registry::Start).
  if (!FeatureIsOff(feature))
    Degradations::Get().Start(feature, why, true);
}

bool FeatureIsOff(std::string_view feature) {
  std::lock_guard _(g_lock);
  const auto *entry = EntryOf(feature);
  return entry && entry->state == State::Off;
}

bool FeatureIsOn(std::string_view feature) {
  std::lock_guard _(g_lock);
  const auto *entry = EntryOf(feature);
  return entry && entry->state == State::On;
}

bool ClassSizeIs(std::string_view feature, const char *type, std::uint32_t expected) {
  auto *found = ClassOf(type);
  if (found && found->size == expected)
    return true;
  FeatureOff(feature, found ? std::format("{} is 0x{:X} bytes, measured as 0x{:X} on 2.50", type,
                                          found->size, expected)
                            : std::format("class {} not found", type));
  // (FeatureOff keeps the first reason only: every mismatch is still named.)
  if (found)
    log::Error("layout check: {} is 0x{:X} bytes, measured as 0x{:X} on 2.50", type, found->size,
               expected);
  else
    log::Error("layout check: class {} not found", type);
  return false;
}

bool PropertyAt(std::string_view feature, const char *type, const char *property,
                std::uint32_t expected) {
  auto *found = ClassOf(type);
  const auto *field = found ? found->GetProperty(property) : nullptr;
  if (field && field->valueOffset == expected)
    return true;
  const auto reason = field ? std::format("{}.{} is at +0x{:X}, measured at +0x{:X} on 2.50", type,
                                          property, field->valueOffset, expected)
                            : std::format("{}.{} not found", type, property);
  FeatureOff(feature, reason);
  log::Error("layout check: {}", reason);
  return false;
}

bool InGameImage(std::uintptr_t address) {
  static const auto s_begin = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
  static const auto s_end = [] {
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(s_begin);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(s_begin + dos->e_lfanew);
    return s_begin + nt->OptionalHeader.SizeOfImage;
  }();
  return address >= s_begin && address < s_end;
}

void CheckAccess() {
  if (g_checked.load(std::memory_order_relaxed))
    return;
  // The game's classes are registered long before the first frame; were they
  // not, the checks wait for them (ten seconds of frames at most).
  static std::uint32_t s_waited = 0;
  if (!ClassOf("entEntity") && ++s_waited < 600)
    return;
  CheckMovement();
  CheckClipLengths();
  CheckLooks();
  CheckThirdPerson();
  CheckPoseStream();
  CheckQuests();
  CheckPrefabs();
  CheckWorkspots();
  CheckVehicleMove();
  CheckPhysics();
  CheckDismembermentGuard();
  CheckSlideGuard();
  CheckDisposeGate();
  CheckBodySlots();
  CheckCrowdSpawners();
  CheckRagdoll();
  CheckActivityMarks();
  g_checked = true;
  const auto summary = AccessSummary();
  if (FeaturesOff().empty())
    log::Info("{}", summary);
  else
    log::Warn("{}", summary);
}

bool AccessChecked() { return g_checked.load(); }

std::string AccessSummary() {
  std::lock_guard _(g_lock);
  int on = 0, off = 0, notAsked = 0;
  std::string list;
  for (const auto *found : Ordered()) {
    const auto &entry = *found;
    if (!list.empty())
      list += ", ";
    list += entry.name;
    switch (entry.state) {
    case State::On:
      ++on;
      list += " on";
      break;
    case State::Off:
      ++off;
      list += std::format(" OFF ({})", entry.why);
      break;
    case State::NotAsked:
      ++notAsked;
      list += std::format(" not asked ({})", entry.why);
      break;
    }
  }
  return std::format("engine: {} features on, {} off{}{}: {}", on, off,
                     notAsked ? std::format(", {} not asked for", notAsked) : "",
                     g_checked.load() ? "" : " (not checked yet)", list);
}

std::string FeaturesOff() {
  std::lock_guard _(g_lock);
  std::string list;
  for (const auto *entry : Ordered())
    if (entry->state == State::Off)
      list += (list.empty() ? "" : ", ") + entry->name;
  return list;
}
} // namespace tandem::plugin::engine
