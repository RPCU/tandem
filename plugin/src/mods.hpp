#pragma once

// Other mods in the plugin (docs/architecture/mod-compat.md): every record
// another game names goes through Resolve before our game uses it, and the
// player hears of each replacement (scripts/tandem/Mods.reds through
// mods_natives.cpp). The rules are core's (tandem/mods.hpp); this holds the
// session's resolver, with our game's own checks (does TweakDB have the
// record, does the depot have the resource), and what the net thread said
// of the other players' mods.

#include "tandem/endpoint.hpp"
#include "tandem/mods.hpp"
#include "tandem/protocol.hpp"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace tandem::plugin {
// One thing for the player (Mods.reds words it, Text.reds).
struct ModNotice {
  enum class What : std::int32_t {
    Replaced = 1,  // `count` of `player`'s records of `kind` shown otherwise (`outcome`)
    Compared = 2,  // `player`'s mods against ours (ModRecords::Counts)
    Seen = 3,      // `player` sees substitutes for `count` of ours (`kind`: the kinds' bits)
    NamesCame = 4, // the names of `player`'s mods we asked for arrived
  };
  What what{};
  proto::PlayerId player{};
  std::string name;
  std::int32_t kind{};
  std::int32_t count{};
  std::int32_t outcome{};
};

class ModRecords {
public:
  static ModRecords &Get();

  // At load (main.cpp): our own mods, as the startup read them.
  void SetOurs(std::vector<mods::Mod> ours);
  std::vector<proto::ModEntry> OursForIdentity();

  // Game thread (natives): what our game uses for `player`'s `record`.
  std::uint64_t Resolve(mods::Kind kind, std::uint64_t record, const proto::RecordHint &hint,
                        proto::PlayerId player);
  // A replacement the scripts found (an NPC's appearance, a radio station):
  // `shown` 0 is nothing (the record's usual look, the radio off).
  void Note(mods::Kind kind, std::uint64_t record, std::uint64_t shown, proto::PlayerId player);
  // Our own look's resources, which our depot must have: if it says it has
  // none of them, its answer is not trusted and no piece is dropped.
  void CheckDepot(const std::vector<proto::AppearanceKey> &ours);

  // Net thread: what the endpoint said; a session ended.
  void OnCompared(const events::RemoteMods &mods);
  void OnNames(const events::ModNamesFrom &names);
  void OnSeen(const events::SubstitutesFrom &seen);
  void Ended();
  // Net thread: what to send (Endpoint::SendSubstitutes, AskModNames).
  std::vector<proto::Substitutes> TakeTold();
  std::vector<proto::PlayerId> TakeNameAsks();

  // Game thread (scripts).
  std::vector<ModNotice> TakeNotices(std::uint64_t nowUs);
  std::vector<proto::PlayerId> Compared();
  // Per mods::kCategoryOrder, then the total: theirs only, or ours only.
  std::vector<std::int32_t> Counts(proto::PlayerId player, bool theirs);
  std::vector<std::string> Names(proto::PlayerId player);
  void AskNames(proto::PlayerId player);
  std::vector<mods::Substitution> Substitutions();
  // (tools/game.sh cmd coop mods) everything, for the log.
  std::vector<std::string> Report();

private:
  bool Known(mods::Kind kind, std::uint64_t record);

  std::mutex m_mutex;
  mods::Resolver m_resolver{
      [this](mods::Kind kind, std::uint64_t record) { return Known(kind, record); }};
  std::vector<mods::Mod> m_ours;
  struct Theirs {
    std::string name;
    mods::Comparison comparison;
    std::vector<std::string> names;
  };
  std::map<proto::PlayerId, Theirs> m_theirs;
  std::vector<ModNotice> m_notices; // from the net thread, for the scripts
  std::vector<proto::PlayerId> m_nameAsks;
  // The depot's answer is trusted (CheckDepot): unknown until our own look.
  enum class Depot { Unchecked, Trusted, Ignored } m_depot{Depot::Unchecked};
};
} // namespace tandem::plugin
