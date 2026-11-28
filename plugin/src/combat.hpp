#pragma once

// Hit outcomes' queues between the net thread and the game thread
// (scripts/tandem/Outcomes.reds through plugin/src/combat_natives.cpp). Kept
// apart from Runtime, like the story: its own messages (proto::IsCombat).

#include "tandem/protocol.hpp"
#include "tandem/vendors.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace tandem::plugin {
class Combat {
public:
  static Combat &Get();

  // Net thread: what the session says (NpcStrike from a guest, which a
  // guest's older NpcHit also becomes; NpcWound, NpcPoise, Difficulty,
  // Rules from the host; AreaHit on our V; the others' Throw and ThrowEnd;
  // a guest's Defended), and what we have to say.
  void PushInbound(proto::Message message);
  std::vector<proto::Message> TakeOutbound();
  // Net thread: the session's rules as the endpoint knows them now.
  void SetRules(proto::Rules rules) {
    m_rules.store(
        static_cast<std::uint32_t>(rules.flags | rules.enemyHealth << 8 | rules.levelGap << 16),
        std::memory_order_relaxed);
  }

  // Game thread. A throw that waited longer than combat::kThrowStaleUs (a
  // stall, a loading screen) is dropped here: its copy would fly late.
  std::vector<proto::Message> TakeInbound();
  void Send(proto::Message message);
  proto::Rules Rules() const {
    const auto rules = m_rules.load(std::memory_order_relaxed);
    return {.flags = static_cast<std::uint8_t>(rules & 0xFF),
            .enemyHealth = static_cast<std::uint8_t>((rules >> 8) & 0xFF),
            .levelGap = static_cast<std::uint8_t>((rules >> 16) & 0xFF)};
  }
  // Throws dropped as stale since the start.
  std::uint32_t StaleThrows() const { return m_staleThrows.load(std::memory_order_relaxed); }
  // Who may drive each player's cars (proto::CarLend, as the session said
  // it; Crew for a player that said nothing). Game thread: noted as the
  // scripts take the session's word.
  void NoteLend(const proto::CarLend &lend);
  proto::Lend LendOf(proto::PlayerId player);
  // The custom paint of each player's car (proto::CarPaint; stock for a
  // player that said nothing), noted the same way, and how many times that
  // player's has changed here (the scripts apply a paint once).
  struct Paint {
    proto::CarPaint paint;
    std::uint32_t changes{};
  };
  void NotePaint(const proto::CarPaint &paint);
  Paint PaintOf(proto::PlayerId player);
  // The session's vendors (proto::NpcVendor, tandem/vendors.hpp), noted the
  // same way: each shared NPC's placed entity, the vendors that died.
  void NoteVendor(const proto::NpcVendor &vendor);
  std::uint64_t VendorOf(proto::NpcId npc);
  bool VendorDead(std::uint64_t entity);
  std::vector<std::uint64_t> TakeDeadVendors();
  void ClearVendors();
  // Net thread, at a session's end: its vendors, lends, paints and rules are not
  // the next one's (an NPC id or a player number it reuses is someone
  // else, and its host says its own rules).
  void EndSession();

private:
  std::map<proto::PlayerId, proto::Lend> m_lends;
  std::map<proto::PlayerId, Paint> m_paints;
  vendors::Book m_vendors;
  struct Inbound {
    proto::Message message;
    std::uint64_t arrivalUs{};
  };
  std::mutex m_lock;
  std::vector<Inbound> m_inbound;
  std::vector<proto::Message> m_outbound;
  // proto::Rules: its flags, its enemyHealth above them, its levelGap above
  // that (none said yet: every flag off).
  std::atomic<std::uint32_t> m_rules{
      static_cast<std::uint32_t>(proto::Rules{}.enemyHealth << 8 | proto::Rules{}.levelGap << 16)};
  std::atomic<std::uint32_t> m_staleThrows{};
};
} // namespace tandem::plugin
