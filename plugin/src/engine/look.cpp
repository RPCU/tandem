// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "look.hpp"

#include <RED4ext/Relocation.hpp>
#include <RED4ext/ResourcePath.hpp>
#include <RED4ext/Scripting/Natives/Generated/world/RuntimeSystemEntityAppearanceChanger.hpp>
#include <RedLib.hpp>

#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace tandem::plugin::engine {
namespace {
// world::EntityAppearanceChangeParameter::Key: one appearance part.
struct AppearanceKey {
  RED4ext::ResourcePath resource; // 00
  RED4ext::CName definition;      // 08
};
static_assert(sizeof(AppearanceKey) == 0x10);

template <class T> struct Span {
  T *begin;
  T *end;
};

// AppearanceChangeSystem_ChangeAppearance2 (Codeware's name for the hash):
// (changer, entity, old keys, new keys, on done, I/O priority).
constexpr std::uint32_t kScheduleAppearanceChange = 386815609;
using ScheduleChange = void (*)(RED4ext::world::RuntimeSystemEntityAppearanceChanger *,
                                RED4ext::WeakHandle<RED4ext::ent::Entity> &, Span<AppearanceKey> *,
                                Span<AppearanceKey> *, const std::function<void()> &,
                                std::uint32_t);

// gameuiICharacterCustomizationState virtual slots (byte offsets in its
// vtable), the ones NCO dressed remote players with on 2.5. Not in RTTI, so
// IsBodyGenderMale is checked against what the game says first: a shifted
// vtable answers wrong there before anything takes a key array.
constexpr std::size_t kIsBodyGenderMale = 0x120;
constexpr std::size_t kGetHeadKeys = 0x190;
constexpr std::size_t kGetBodyKeys = 0x1A0;
constexpr std::size_t kGetArmsKeys = 0x1B0;

using IsMaleFn = bool (*)(const void *);
using GetKeysFn = bool (*)(const void *, RED4ext::CName group, bool thirdPerson,
                           RED4ext::DynArray<AppearanceKey> &);

template <class Fn> Fn Slot(const void *object, std::size_t offset) {
  const auto *vtable = *reinterpret_cast<void *const *const *>(object);
  return reinterpret_cast<Fn>(vtable[offset / sizeof(void *)]);
}

// The groups that make a whole third-person V: head, body, arms.
void CollectKeys(const void *state, bool male, RED4ext::DynArray<AppearanceKey> &keys) {
  const auto head = Slot<GetKeysFn>(state, kGetHeadKeys);
  const auto body = Slot<GetKeysFn>(state, kGetBodyKeys);
  const auto arms = Slot<GetKeysFn>(state, kGetArmsKeys);
  for (const char *group : {"TPP", "face", "hairs"})
    head(state, group, true, keys);
  if (male)
    head(state, "beards", true, keys);
  for (const char *group : {"TPP_Body", "breast", "lifted_feet", "flat_feet"})
    body(state, group, true, keys);
  for (const char *group : {"holstered_default", "nails"})
    arms(state, group, true, keys);
}

// What the asynchronous change reads until it is done.
struct PendingChange {
  RED4ext::DynArray<AppearanceKey> keys;
  RED4ext::Handle<RED4ext::game::ui::ICharacterCustomizationState> state;
};
} // namespace

namespace {
bool UsableState(const RED4ext::Handle<RED4ext::game::ui::ICharacterCustomizationState> &state,
                 bool male) {
  if (!state) {
    log::Warn("look: no customization state");
    return false;
  }
  const auto *type = state->GetType();
  if (!type || type->GetName() != RED4ext::CName("gameuiCharacterCustomizationState")) {
    log::Warn("look: not a character customization state");
    return false;
  }
  const void *raw = state.GetPtr();
  if (Slot<IsMaleFn>(raw, kIsBodyGenderMale)(raw) != male) {
    log::Error("look: the state's vtable is not the one measured (IsBodyGenderMale disagrees), "
               "not used");
    return false;
  }
  return true;
}

std::int32_t Schedule(const RED4ext::Handle<RED4ext::ent::Entity> &entity,
                      std::shared_ptr<PendingChange> pending) {
  auto *changer = Red::GetRuntimeSystem<Red::world::RuntimeSystemEntityAppearanceChanger>();
  if (!changer) {
    log::Warn("look: no appearance changer");
    return -1;
  }
  RED4ext::WeakHandle<RED4ext::ent::Entity> target = entity;
  Span<AppearanceKey> none{nullptr, nullptr};
  Span<AppearanceKey> wanted{pending->keys.Data(), pending->keys.Data() + pending->keys.Size()};
  const auto count = static_cast<std::int32_t>(pending->keys.Size());
  const auto id = entity->entityID.hash;
  // The change is asynchronous: the keys live until it says it is done.
  const std::function<void()> done = [pending, id] {
    log::Info("look: entity {:x} dressed ({} keys)", id, pending->keys.Size());
  };
  static RED4ext::UniversalRelocFunc<ScheduleChange> schedule(kScheduleAppearanceChange);
  schedule(changer, target, &none, &wanted, done, 0);
  log::Info("look: {} keys scheduled on entity {:x}", count, id);
  return count;
}

// What each entity was last given (game thread only).
std::map<std::uint64_t, std::uint64_t> g_applied;

std::uint64_t HashOf(const std::vector<proto::AppearanceKey> &keys) {
  std::uint64_t hash = 0xCBF29CE484222325;
  for (const auto &key : keys)
    for (const auto value : {key.resource, key.definition})
      hash = (hash ^ value) * 0x100000001B3;
  return hash;
}
} // namespace

std::vector<proto::AppearanceKey>
CollectLook(const RED4ext::Handle<RED4ext::game::ui::ICharacterCustomizationState> &state,
            bool male) {
  if (!UsableState(state, male))
    return {};
  RED4ext::DynArray<AppearanceKey> keys;
  CollectKeys(state.GetPtr(), male, keys);
  std::vector<proto::AppearanceKey> out;
  for (const auto &key : keys)
    out.push_back({key.resource.hash, key.definition.hash});
  return out;
}

std::int32_t ScheduleLook(const RED4ext::Handle<RED4ext::ent::Entity> &entity,
                          const std::vector<proto::AppearanceKey> &keys) {
  if (!entity || keys.empty())
    return -1;
  const auto hash = HashOf(keys);
  auto &applied = g_applied[entity->entityID.hash];
  if (applied == hash)
    return 0;
  applied = hash;
  auto pending = std::make_shared<PendingChange>();
  for (const auto &key : keys) {
    AppearanceKey k{};
    k.resource.hash = key.resource;
    k.definition = RED4ext::CName(key.definition);
    pending->keys.EmplaceBack(k);
  }
  return Schedule(entity, std::move(pending));
}

std::int32_t
ApplyLook(const RED4ext::Handle<RED4ext::ent::Entity> &entity,
          const RED4ext::Handle<RED4ext::game::ui::ICharacterCustomizationState> &state,
          bool male) {
  if (!entity || !UsableState(state, male))
    return -1;
  auto pending = std::make_shared<PendingChange>();
  pending->state = state;
  CollectKeys(state.GetPtr(), male, pending->keys);
  if (pending->keys.Size() == 0) {
    log::Warn("look: the state resolved no appearance keys");
    return 0;
  }
  return Schedule(entity, std::move(pending));
}
} // namespace tandem::plugin::engine
