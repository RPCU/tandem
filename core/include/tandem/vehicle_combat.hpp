#pragma once

// Vehicle combat (docs/architecture/vehicle-combat.md): the rules every game
// applies the same way, kept out of the engine glue so they are tested
// alone. Damage states and when a copy blows up, the gunner seat of a
// vehicle (first come, first served, freed when someone goes), how a V left
// a vehicle, a car running into an NPC, and two players' cars touching.

#include "tandem/protocol.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace tandem::vehiclecombat {
// ---- damage ----

// The destruction grid (VehicleGridDestructionEvent.state: 16 cells, each
// 0..1) in bytes of 1/255, and back.
std::array<std::uint8_t, proto::kDamageGrid> PackGrid(std::span<const float> cells);
float GridCell(const std::array<std::uint8_t, proto::kDamageGrid> &grid, std::size_t cell);

// `incoming` is newer than `kept` (the same vehicle's): its revision is
// later, counting round the 32-bit wrap.
bool Supersedes(const proto::VehicleDamage &kept, const proto::VehicleDamage &incoming);

// What a copy does on hearing a damage state, given what it had before
// (nothing: it never heard of this vehicle). A destruction heard within
// kFreshExplosionUs of its explosion is played (the game's own blast, fire,
// sound, parts flying); one heard later (a late joiner, a resync, a copy
// spawned after) is a wreck put there, never blown up again.
inline constexpr std::uint64_t kFreshExplosionUs = 3'000'000;
enum class Destruction { None, Explode, Wreck };
Destruction OnArrival(const proto::VehicleDamage &incoming,
                      const std::optional<proto::VehicleDamage> &before, std::uint64_t hostNowUs);

// A hit applied to a damage state by its owner, with the vehicle's maximum
// health in points: health down (a wreck at none, then nothing more), a tyre
// flat, a window broken. The game applies hits with its own pools; this is
// the rule of record (the mock player's vehicles, the tests).
proto::VehicleDamage Apply(proto::VehicleDamage state, const proto::VehicleHit &hit,
                           float maxHealth, std::uint64_t hostNowUs);

// ---- exits ----

// How a V left a vehicle, from the game's exit state that ran (at most one
// is true) and what it left: a collision exit is a bike's knock-off on a
// bike, a crash ejection in a car; any exit while the vehicle is destroyed
// is an explosion's.
struct ExitSeen {
  bool speed{};     // SpeedExitingEvents: jumped out on the move
  bool collision{}; // CollisionExitingEvents
  bool death{};     // DeathExitingEvents
  bool forced{};    // another ImmediateExitWithForceEvents
  bool bike{};
  bool destroyed{}; // the vehicle is a wreck
};
proto::ExitKind Classify(const ExitSeen &seen);

// A vehicle left this fast rolls on with nobody at the wheel: its game keeps
// sending its state until it is slower than kRestSpeed, or kRollUs is over.
inline constexpr float kRollSpeed = 2.f;
inline constexpr float kRestSpeed = 0.5f;
inline constexpr std::uint64_t kRollUs = 12'000'000;
bool Rolls(proto::ExitKind kind, float speed, bool driver);

// ---- running into NPCs ----

// The game's own thresholds (final.reds ProcessVehicleHit, AIGeneralSettings
// defaults): a car this fast relative to the NPC kills it outright, a
// crowd NPC or a civilian already at the lower one; a knock-down from
// kKnockdownSpeed (tandem's, to check in game).
inline constexpr float kKillSpeed = 25.f;
inline constexpr float kCrowdKillSpeed = 20.f;
inline constexpr float kKnockdownSpeed = 4.f;
std::uint8_t RamFlags(float relativeSpeed, bool crowd);

// ---- two players' cars ----

// Each driver's game runs its own car's side of a contact with the other's
// copy, and sends the push its car gave. The other game applies a push only
// when it did not feel that contact itself: a push is held kWindowUs, and
// our own car touching the sender's copy within kWindowUs of it cancels it
// (the copy is drawn a little in the past, so our side of the contact may
// come just before or just after theirs). One push per contact, never two.
class BumpLedger {
public:
  static constexpr std::uint64_t kWindowUs = 400'000;
  static constexpr std::uint64_t kStaleUs = 1'500'000;
  static constexpr float kMaxImpulse = 25.f; // m/s: a push bigger than this is a bug

  struct Push {
    proto::PlayerId from{};
    proto::Vec3 impulse;
    proto::Vec3 position;
  };

  // Our car touched `other`'s car (its copy here) at `atUs` (host clock).
  void Contact(proto::PlayerId other, std::uint64_t atUs);
  // `bump.player`'s car says it gave ours `bump.impulse` (held until due).
  void Receive(const proto::VehicleBump &bump, std::uint64_t nowUs);
  // The pushes to give our car now: held long enough, not felt here, not
  // too old (behind a loading screen), clamped to kMaxImpulse.
  std::vector<Push> Due(std::uint64_t nowUs);
  void Forget(proto::PlayerId player);
  void Clear();
  std::size_t Held() const { return m_held.size(); }

private:
  std::map<proto::PlayerId, std::uint64_t> m_contactUs;
  std::vector<proto::VehicleBump> m_held;
  // Felt here: our contact was within the window of this bump's.
  bool Felt(const proto::VehicleBump &bump) const;
};

// ---- the gunner seat ----

// A vehicle owner's side of its gunner seats: the first V to ask has the
// seat, a second is refused (it hears who has it), the one holding it can
// leave, and a V holds at most one seat. Never the vehicle's own driver.
class Turrets {
public:
  // `request.player` asks for, or leaves, a seat of one of our vehicles.
  // Returns the seats whose holder changed (each to say to everyone).
  std::vector<proto::Gunner> Ask(const proto::GunnerRequest &request);
  // `player` is gone: every seat it held is free.
  std::vector<proto::Gunner> Release(proto::PlayerId player);
  // The vehicle's seat is free again (our game could not seat them, the
  // vehicle went); nullopt when nobody held it.
  std::optional<proto::Gunner> Free(proto::VehicleRef vehicle);
  // Another game's word on a seat (the host freed it when the gunner left).
  void Set(const proto::Gunner &gunner);
  const std::map<proto::VehicleRef, proto::Gunner> &Seats() const { return m_seats; }
  void Clear() { m_seats.clear(); }

private:
  proto::Gunner Change(proto::VehicleRef vehicle, proto::PlayerId gunner, std::uint64_t seat);
  std::map<proto::VehicleRef, proto::Gunner> m_seats;
  std::uint32_t m_revision{};
};
} // namespace tandem::vehiclecombat
