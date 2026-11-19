// What scripts/tandem/Consistency.reds can call: `TandemSync` static
// functions and their structs (docs/architecture/consistency.md, Consistency). Declared
// on the script side in scripts/tandem/SyncNatives.reds: keep the two in
// sync (tools/check-natives.py reads every pair). All of these run on the
// game thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "runtime.hpp"

#include "tandem/interpolation.hpp"

#include <RedLib.hpp>

namespace tandem::plugin {
// One NPC the host shares, where it is now (its track) or was announced.
// Plain fields only (the script VM aligns struct locals to 8).
struct TandemSyncNpc {
  std::uint32_t npc{};
  Red::TweakDBID record;
  Red::CName appearance;
  float x{}, y{}, z{};
  float yaw{};
};
static_assert(alignof(TandemSyncNpc) <= 8, "the script VM aligns struct locals to 8");

// One player in the session.
struct TandemSyncPlayer {
  std::uint32_t player{};
  Red::CString name;
  std::int32_t body{};
};

struct TandemSync : Red::IScriptable {
  // Guest: every NPC the host shares as our session's record has it: the
  // proxies our game must show, no more, no less.
  static Red::DynArray<TandemSyncNpc> SharedNpcs() {
    auto &runtime = Runtime::Get();
    Red::DynArray<TandemSyncNpc> out;
    for (const auto &spawn : runtime.SharedNpcs()) {
      TandemSyncNpc npc{.npc = spawn.npc,
                        .record = Red::TweakDBID(spawn.record),
                        .appearance = Red::CName(spawn.appearance),
                        .x = spawn.position.x,
                        .y = spawn.position.y,
                        .z = spawn.position.z,
                        .yaw = spawn.yaw};
      // Where the host's states put it now, if they have.
      if (const auto view = runtime.ViewOf(kNpcPuppetBase + spawn.npc); view.valid) {
        npc.x = view.position.x;
        npc.y = view.position.y;
        npc.z = view.position.z;
        npc.yaw = view.yaw;
      }
      out.EmplaceBack(npc);
    }
    return out;
  }
  static bool IsNpcShared(std::uint32_t npc) { return Runtime::Get().IsNpcShared(npc); }
  static std::int32_t SharedNpcCount() {
    return static_cast<std::int32_t>(Runtime::Get().SharedNpcs().size());
  }

  // The players in the session (us too).
  static Red::DynArray<TandemSyncPlayer> Players() {
    Red::DynArray<TandemSyncPlayer> out;
    for (const auto &info : Runtime::Get().RosterPlayers())
      out.EmplaceBack(TandemSyncPlayer{.player = info.id,
                                       .name = Red::CString(info.name.c_str()),
                                       .body = static_cast<std::int32_t>(info.body)});
    return out;
  }
  static bool InSession(std::uint32_t player) { return Runtime::Get().InRoster(player); }

  // Our world was rebuilt around our V (proto::kEpoch*: 1 a loading screen,
  // 2 fast travel, 3 a time skip, 4 back from a standstill: photo mode).
  static void WorldRebuilt(std::int32_t reason) {
    if (reason >= proto::kEpochLoading && reason <= proto::kEpochFrozen)
      Runtime::Get().RequestEpoch(static_cast<std::uint8_t>(reason));
  }

  RTTI_IMPL_TYPEINFO(TandemSync);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemSyncNpc, "TandemSyncNpc", {
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(appearance);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(yaw);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemSyncPlayer, "TandemSyncPlayer", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(name);
  RTTI_PROPERTY(body);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemSync, "TandemSync", {
  RTTI_METHOD(SharedNpcs);
  RTTI_METHOD(IsNpcShared);
  RTTI_METHOD(SharedNpcCount);
  RTTI_METHOD(Players);
  RTTI_METHOD(InSession);
  RTTI_METHOD(WorldRebuilt);
});
