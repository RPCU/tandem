#pragma once

// Vendors in co-op (docs/architecture/progression.md, "Vendors"): a shop,
// a ripperdoc or a clothing store is an NPC of the world, its stock, prices
// and money each player's own. A player whose game shows the vendor as a
// proxy (another game runs the NPC) opens its own game's vendor of the same
// placed entity, which its own save keeps, exactly as in single player.
//
//   Decide  whether a shop opens for our V, and why not (the notice);
//   Book    the vendors the session named: each shared NPC's static entity,
//           and the entities whose vendor died in the session.

#include "tandem/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace tandem::vendors {
// How close our V stands to a vendor to trade with it (metres): the game's
// own interaction reach for an NPC is about this.
inline constexpr float kReach = 2.5f;

// The answer to "may our V trade with this vendor now".
//   Open      open our own game's vendor of that entity;
//   NotVendor the NPC's record has no vendor: no prompt;
//   Unknown   its owner named no placed entity (a spawned NPC of a vendor
//             record is nobody's shop): no prompt;
//   Far       out of reach: no prompt;
//   Dead      it died (the proxy is a corpse, the session says so, or our own
//             world's copy is dead: our save's word);
//   Busy      our V cannot now (combat, down, a scene, a menu, a trade);
//   NotHere   our world has no such vendor (our story has not put it there,
//             or not yet): the shop stays shut for us, as in our own game;
//   Locked    our world has it, and its access rules refuse our V (the
//             vendor record's AccessPrereqs: our story, our street cred).
enum class Shop : std::uint8_t { Open, NotVendor, Unknown, Far, Dead, Busy, NotHere, Locked };

// What the game knows when our V looks at a vendor's proxy.
struct Ask {
  bool vendorRecord{};    // its character record has a vendor
  std::uint64_t entity{}; // the placed entity its owner named (0: none)
  float distance{};       // from our V, metres
  bool proxyDead{};       // the owner's NPC is dead
  bool sessionDead{};     // the session says that entity's vendor died
  bool free{true};        // our V may trade now
  bool ownFound{};        // our world has an entity of that id
  bool ownVendor{};       // ...and it is a vendor (its record has one)
  bool ownAlive{};        // ...and it is alive in our world
  bool accessible{};      // ...and its access rules let our V in
};

Shop Decide(const Ask &ask);
// The prompt shows (the answer is worth a press: Open, or a reason to say).
bool Prompts(Shop shop);
const char *NameOf(Shop shop);

// The session's vendors, as one game hears them (the plugin's copy of what
// its endpoint keeps). Bounded: a session names a few dozen.
class Book {
public:
  static constexpr std::size_t kMaxLive = 512;
  static constexpr std::size_t kMaxDead = 256;

  // What the session said (NpcVendor). True when it is news: a new vendor
  // or a new death.
  bool Note(const proto::NpcVendor &vendor);
  // The placed entity of a shared NPC (0: not a vendor, or not said).
  std::uint64_t EntityOf(proto::NpcId npc) const;
  // That entity's vendor died in the session.
  bool Dead(std::uint64_t entity) const { return m_dead.contains(entity); }
  // The deaths heard since the last call, once each.
  std::vector<std::uint64_t> TakeDead();
  std::size_t Live() const { return m_live.size(); }
  std::size_t DeadCount() const { return m_dead.size(); }
  // A new session.
  void Clear();

private:
  std::map<proto::NpcId, std::uint64_t> m_live;
  std::set<std::uint64_t> m_dead;
  std::vector<std::uint64_t> m_news;
};
} // namespace tandem::vendors
