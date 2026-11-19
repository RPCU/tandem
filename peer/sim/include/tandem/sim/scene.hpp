#pragma once

// What one simulated game knows of the session (docs/architecture/sim.md):
// the other players and, as a guest, the host's NPCs, kept from the
// endpoint's events. The models read it and write protocol messages to an
// Outbox; neither touches the network, so they run in tests without one.

#include "tandem/endpoint.hpp"
#include "tandem/protocol.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace tandem::sim {
// A message a model wants sent. `to`: the one player it is for, when the
// endpoint call takes one (a reply, a reward); kNoPlayer otherwise.
struct Outgoing {
  proto::Message message;
  proto::PlayerId to{proto::kNoPlayer};
};
using Outbox = std::vector<Outgoing>;

struct SeenPlayer {
  proto::PlayerId id{};
  bool heard{}; // a state of its V came
  proto::Vec3 position, velocity;
  float yaw{};
  std::uint64_t heardUs{};
  proto::Life life{};
  std::uint8_t stealth{}; // kStealth*
  float visibility{1.f};
  proto::Place place{};
  std::uint8_t elsewhere{}; // kElsewhere*
  bool paused{};
  std::uint8_t abilities{}; // kTime*
  float timeScale{1.f};
  proto::Seat seat;
  proto::TransitKind transit{};
  std::uint8_t tier{1}, story{}; // StoryState
  std::optional<proto::VehicleState> car;
  // Fit to be seen, fought or stood beside: in the city, in its body.
  bool Present() const { return heard && place == proto::Place::City; }
  bool Driving() const { return seat.vehicle != 0 && (seat.owner == id || seat.owner == 0); }
};

struct SeenNpc {
  proto::NpcId id{};
  std::uint64_t record{};
  proto::Vec3 position;
  float yaw{};
  float health{1.f}; // fraction
  bool live{true};   // shared now: can be hit, hacked, grabbed
  bool body{};       // dead or unconscious, lying there: can be carried
  bool dead{};
  proto::PlayerId holder{};
  std::uint8_t detected{}; // bit (player - 1)
};

struct Scene {
  proto::PlayerId self{}, host{};
  std::map<proto::PlayerId, SeenPlayer> players; // everyone but us
  std::map<proto::NpcId, SeenNpc> npcs;
  std::map<proto::WorldItemId, proto::WorldItem> items; // lying in the world
  std::optional<proto::PhoneCall> call;                 // the host's, while it is up
  std::optional<proto::ChoiceHub> hub;                  // open now

  // Takes what an endpoint reported. A guest keeps the host's NPCs from it;
  // a host's NPCs are its world's (HostWorld::Fill).
  void On(const SessionEvent &event, std::uint64_t nowUs);
  // The player a companion keeps to: the host for a guest, the first guest
  // heard for a host (nullptr: nobody yet).
  const SeenPlayer *Partner() const;
  const SeenPlayer *Find(proto::PlayerId player) const;
  bool AnyoneElseUp() const;
};

// Hands a model's message to the endpoint call a game makes for it. Host:
// its world, its story, its own V. Guest: its own V and its requests.
// False: nothing in this role sends such a message.
bool Deliver(Endpoint &endpoint, const Outgoing &outgoing);
} // namespace tandem::sim
