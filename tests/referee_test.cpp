// The referee's rules, fed by hand, and a clean session over loopback that
// breaks none (docs/architecture/sim.md).

#include "ports.hpp"
#include "tandem/regions.hpp"
#include "tandem/sim/coverage.hpp"
#include "tandem/sim/referee.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace std::chrono_literals;
using namespace tandem;
using sim::Referee;

namespace {
constexpr std::uint64_t kSecond = 1'000'000;

// The rules broken so far, in order.
std::vector<std::string> Rules(Referee &referee, bool hints = false) {
  std::vector<std::string> out;
  for (const auto &finding : referee.Take())
    if (finding.hint == hints)
      out.push_back(finding.rule);
  return out;
}

bool Broke(Referee &referee, std::string_view rule, bool hint = false) {
  const auto rules = Rules(referee, hint);
  return std::ranges::find(rules, rule) != rules.end();
}

// A guest's referee, welcomed as player 2 by host 1, with a roster.
// `silence`: a host that stops sending digests is told too.
Referee Guest(bool silence = false, std::uint64_t now = kSecond) {
  Referee::Options options;
  if (!silence)
    options.limits.digestSilenceUs = 1'000'000 * kSecond;
  Referee referee(false, options);
  referee.Received(0, proto::Welcome{.you = 2, .host = 1}, now);
  referee.Received(1, proto::Roster{{{1, "host", {}}, {2, "us", {}}, {3, "other", {}}}}, now);
  return referee;
}

// A host's referee with guests 2 and 3.
Referee Host(std::uint64_t now = kSecond) {
  Referee referee(true);
  referee.Sent(2, proto::Welcome{.you = 2, .host = 1}, now);
  referee.Sent(3, proto::Welcome{.you = 3, .host = 1}, now);
  referee.Sent(0, proto::Roster{{{1, "us", {}}, {2, "a", {}}, {3, "b", {}}}}, now);
  return referee;
}
} // namespace

TEST_CASE("referee: an owner's word on an NPC goes only to the games that show it") {
  auto referee = Guest();
  referee.Received(1, proto::NpcSpawn{.npc = 7, .record = 1}, kSecond);
  referee.Received(1, proto::PlayerHit{.player = 2, .npc = 7, .physical = 1.f}, kSecond);
  CHECK(Rules(referee).empty());
  // It leaves our view (a link broke, its region's edge): what was in
  // flight may still land...
  referee.Received(1, proto::NpcGone{.npc = 7, .reason = proto::kGoneAway}, 2 * kSecond);
  referee.Received(1, proto::NpcShotAt{.player = 2, .npc = 7, .timeUs = 1}, 2 * kSecond + 500'000);
  CHECK(Rules(referee).empty());
  // ...then nothing its owner says to one V about it is for us.
  const auto later = 4 * kSecond;
  referee.Received(1, proto::PlayerHit{.player = 2, .npc = 7, .physical = 2.f}, later);
  referee.Received(1, proto::NpcCue{.npc = 7, .player = 2, .cue = proto::NpcCueKind::CombatStart},
                   later);
  referee.Received(1, proto::NpcShotAt{.player = 2, .npc = 7, .timeUs = 2}, later);
  referee.Received(1, proto::PlayerHack{.player = 2, .npc = 7, .action = 1}, later);
  referee.Received(1, proto::NpcAnims{.npcs = {{.npc = 7}}}, later);
  CHECK(Rules(referee) == std::vector<std::string>(5, "authority.not-shared"));
  // Back in our view (the link formed again): its word is ours again.
  referee.Received(1, proto::NpcSpawn{.npc = 7, .record = 1}, 5 * kSecond);
  referee.Received(1, proto::PlayerHit{.player = 2, .npc = 7, .physical = 3.f}, 5 * kSecond);
  // A corpse's word is not checked (wounds, holds follow a death).
  referee.Received(1, proto::NpcGone{.npc = 7, .reason = proto::kGoneDead}, 6 * kSecond);
  referee.Received(1, proto::NpcAnims{.npcs = {{.npc = 7}}}, 8 * kSecond);
  CHECK(Rules(referee).empty());
  // The host letting go of one of ours (nobody could take it over, or our
  // set named one its record no longer holds) is not another's word on it.
  const auto ours = regions::MakeNpcId(2, 1);
  referee.Sent(1, proto::NpcSpawn{.npc = ours, .record = 2}, 9 * kSecond);
  referee.Received(1, proto::NpcGone{.npc = ours, .reason = proto::kGoneAway}, 9 * kSecond);
  CHECK(Rules(referee).empty());
}

TEST_CASE("referee: every owner sends its NPCs' motion, numbered and within its rate") {
  auto host = Host();
  // A guest's NPCs go compact too (protocol 21), several messages a tick.
  const auto ours = regions::MakeNpcId(2, 1);
  for (std::uint32_t seq = 1; seq <= 4; ++seq)
    host.Received(2, proto::NpcMotion{.seq = seq, .npcs = {{.npc = ours}}}, kSecond);
  CHECK(Rules(host).empty());
  host.Received(2, proto::NpcMotion{.seq = 4, .npcs = {{.npc = ours}}}, kSecond);
  CHECK(Broke(host, "oneshot.seq-repeated"));
  auto flood = Host();
  for (std::uint32_t seq = 1; seq <= 1000; ++seq)
    flood.Received(2, proto::NpcMotion{.seq = seq}, kSecond + seq * 1000);
  CHECK(Broke(flood, "rate.over-budget"));
}

TEST_CASE("referee: a quest interaction is a guest's question and the host's answer to it") {
  auto host = Host();
  host.Received(2, proto::QuestInteract{.target = 9, .seq = 1, .player = 2}, kSecond);
  CHECK(Rules(host).empty());
  // A guest never answers, and asks in its own name only.
  host.Received(2, proto::QuestInteract{.target = 9, .verdict = proto::kInteractPerformed},
                kSecond);
  host.Received(3, proto::QuestInteract{.target = 9, .seq = 2, .player = 2}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.host-only", "authority.not-yours"});
  auto guest = Guest();
  guest.Received(
      1, proto::QuestInteract{.target = 9, .player = 2, .verdict = proto::kInteractHostOnly},
      kSecond);
  CHECK(Rules(guest).empty());
  // The host never asks us, and never tells us another's answer.
  guest.Received(1, proto::QuestInteract{.target = 9, .player = 2}, kSecond);
  guest.Received(
      1, proto::QuestInteract{.target = 9, .player = 3, .verdict = proto::kInteractLocal}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.guest-only", "authority.misdelivered"});
}

TEST_CASE("referee: a guest may not say what is the host's") {
  auto referee = Host();
  referee.Received(2, proto::NpcSpawn{.npc = 7}, kSecond);
  referee.Received(2, proto::World{}, kSecond);
  referee.Received(2, proto::QuestReward{.reward = 1}, kSecond);
  referee.Received(2, proto::NpcHold{.npc = 7, .player = 2}, kSecond);
  CHECK(Rules(referee) == std::vector<std::string>{"authority.not-owner", "authority.host-only",
                                                   "authority.host-only", "authority.not-owner"});
  // Since regions a guest runs NPCs of its own, in its range of ids.
  const auto ours = regions::MakeNpcId(2, 1);
  referee.Received(2, proto::NpcSpawn{.npc = ours, .record = 3}, kSecond);
  referee.Received(2, proto::NpcHealth{.npc = ours, .health = 0.5f}, kSecond);
  referee.Received(2, proto::NpcStrike{.npc = ours, .damage = 1.f}, kSecond);
  CHECK(Rules(referee).empty());
  referee.Received(3, proto::NpcHealth{.npc = ours, .health = 0.4f}, kSecond);
  referee.Received(3, proto::NpcSpawn{.npc = regions::MakeNpcId(2, 2)}, kSecond);
  CHECK(Rules(referee) == std::vector<std::string>(2, "authority.not-owner"));
  // ...until the host gives it to another.
  referee.Sent(0, proto::NpcOwner{.npc = ours, .owner = 3}, kSecond);
  referee.Received(3, proto::NpcHealth{.npc = ours, .health = 0.4f}, kSecond);
  referee.Received(2, proto::NpcHealth{.npc = ours, .health = 0.3f}, kSecond);
  CHECK(Rules(referee) == std::vector<std::string>{"authority.not-owner"});
  // ...nor speak for another player.
  referee.Received(2, proto::PlayerState{.player = 3, .seq = 1}, kSecond);
  referee.Received(2, proto::Vitals{.player = 1, .life = proto::Life::Down}, kSecond);
  CHECK(Rules(referee) == std::vector<std::string>(2, "authority.not-yours"));
  // Its own name, or none, is fine.
  referee.Received(2, proto::PlayerState{.player = 2, .seq = 2}, kSecond);
  referee.Received(2, proto::Held{}, kSecond);
  CHECK(Rules(referee).empty());
}

TEST_CASE("referee: a shared vehicle's word is its owner's, wherever it was handed") {
  // Host side: a guest speaks for the vehicles its game runs only.
  auto host = Host();
  const auto theirs = proto::MakeTrafficId(2, 1);
  host.Received(2, proto::TrafficSpawn{.id = theirs, .record = 5}, kSecond);
  host.Received(2, proto::TrafficStatus{.id = theirs, .health = 0.5f}, kSecond);
  host.Received(2, proto::Chase{.vehicle = theirs, .target = 3}, kSecond);
  CHECK(Rules(host).empty());
  host.Received(3, proto::TrafficStatus{.id = theirs, .health = 0.f}, kSecond);
  host.Received(3, proto::TrafficGone{.id = theirs}, kSecond);
  host.Received(3, proto::TrafficSpawn{.id = proto::MakeTrafficId(2, 2)}, kSecond);
  host.Received(3, proto::TrafficHandoverState{.id = theirs, .to = 1}, kSecond);
  host.Received(3, proto::VehicleDamage{.vehicle = proto::StreetCar(theirs)}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>(5, "authority.not-owner"));
  // Who runs it, and the ask to give it away, are the host's to say.
  host.Received(2, proto::TrafficOwnership{.id = theirs, .owner = 2}, kSecond);
  host.Received(2, proto::TrafficHandover{.id = theirs, .to = 3}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>(2, "authority.host-only"));
  // Handed to player 3: its word from now on, 2's no longer.
  host.Received(2, proto::TrafficHandoverState{.id = theirs, .to = 3}, kSecond);
  host.Sent(0, proto::TrafficOwnership{.id = theirs, .owner = 3}, kSecond);
  host.Received(3, proto::TrafficStatus{.id = theirs, .health = 0.4f}, kSecond);
  host.Received(2, proto::TrafficStatus{.id = theirs, .health = 0.3f}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.not-owner"});

  // Guest side: requests and hand-overs reach us only for what we run.
  auto guest = Guest();
  const auto ours = proto::MakeTrafficId(2, 1), hosts = proto::MakeTrafficId(1, 1);
  guest.Sent(1, proto::TrafficSpawn{.id = ours}, kSecond);
  guest.Received(1, proto::TrafficHit{.id = ours, .damage = 1.f}, kSecond);
  guest.Received(1, proto::TrafficClaim{.id = ours}, kSecond);
  guest.Received(1, proto::TrafficHandover{.id = ours, .to = 1}, kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1, proto::TrafficHit{.id = hosts, .damage = 1.f}, kSecond);
  guest.Received(1, proto::TrafficHandover{.id = hosts, .to = 3}, kSecond);
  guest.Received(1, proto::TrafficStatus{.id = ours, .health = 0.f}, kSecond);
  guest.Received(1, proto::TrafficHandoverState{.id = hosts, .to = 3}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.not-owner", "authority.not-owner",
                                                 "authority.not-owner", "authority.misdelivered"});
  // The host's vehicle handed to us: ours from its state on.
  guest.Received(1, proto::TrafficHandoverState{.id = hosts, .from = 1, .to = 2}, kSecond);
  guest.Received(1, proto::TrafficHit{.id = hosts, .damage = 1.f}, kSecond);
  guest.Received(1, proto::TrafficStatus{.id = hosts, .health = 0.f}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.not-owner"});
}

TEST_CASE("referee: a quickhack on a shared vehicle is a player's, passed on, never back") {
  auto guest = Guest();
  const auto hosts = proto::MakeTrafficId(1, 1);
  guest.Received(1,
                 proto::TrafficHack{
                     .player = 3, .id = hosts, .hack = proto::CarHack::ForceBrakes, .seconds = 8.f},
                 kSecond);
  CHECK(Rules(guest).empty());
  // Our own back, an unnamed vehicle, a length below nothing.
  guest.Received(1, proto::TrafficHack{.player = 2, .id = hosts}, kSecond);
  guest.Received(1, proto::TrafficHack{.player = 3}, kSecond);
  guest.Received(1, proto::TrafficHack{.player = 3, .id = hosts, .seconds = -1.f}, kSecond);
  CHECK(Rules(guest) ==
        std::vector<std::string>{"authority.own-state-back", "value.range", "value.range"});
  // Host: a guest says it in its own name.
  auto host = Host();
  host.Received(2, proto::TrafficHack{.player = 2, .id = hosts, .hack = proto::CarHack::Accelerate},
                kSecond);
  CHECK(Rules(host).empty());
}

TEST_CASE("referee: a car's paint is its owner's word, passed on, never back") {
  auto guest = Guest();
  guest.Received(1,
                 proto::CarPaint{.player = 3,
                                 .vehicle = 9,
                                 .flags = proto::kPaintPrimary | proto::kPaintLights,
                                 .primaryR = 255,
                                 .lightsHue = 120.f},
                 kSecond);
  guest.Received(1, proto::CarPaint{.player = 3}, kSecond); // back to stock paint
  CHECK(Rules(guest).empty());
  // Our own back, a player nobody knows, a flag of no version, a second
  // colour without a first, a hue that is no number.
  guest.Received(1, proto::CarPaint{.player = 2}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.own-state-back"});
  guest.Received(1, proto::CarPaint{.player = 9}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"ids.unknown-player"});
  guest.Received(1, proto::CarPaint{.player = 3, .vehicle = 9, .flags = 32}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(1, proto::CarPaint{.player = 3, .vehicle = 9, .flags = proto::kPaintSecondary},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(1,
                 proto::CarPaint{.player = 3,
                                 .vehicle = 9,
                                 .flags = proto::kPaintLights,
                                 .lightsHue = std::numeric_limits<float>::quiet_NaN()},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.not-finite"});
  // Host: a guest says it in its own name, never in another's.
  auto host = Host();
  host.Received(2, proto::CarPaint{.player = 2, .vehicle = 9, .flags = proto::kPaintPrimary},
                kSecond);
  CHECK(Rules(host).empty());
}

TEST_CASE("referee: a remote view is its player's word, as its game leaves it") {
  const auto car = static_cast<std::uint32_t>(proto::RemoteSort::Vehicle);
  const auto camera = static_cast<std::uint32_t>(proto::RemoteSort::Camera);
  auto guest = Guest();
  guest.Received(1,
                 proto::Activity{.player = 3,
                                 .kind = proto::ActivityKind::Remote,
                                 .device = 5,
                                 .entry = car,
                                 .view = {10.f, 20.f, 3.f},
                                 .traffic = 0x01000004u},
                 kSecond);
  guest.Received(1, proto::Activity{.player = 3}, kSecond);
  CHECK(Rules(guest).empty());
  // A camera that names a car, a remote view that holds its body, a
  // workspot with a view, a view that is no place.
  guest.Received(
      1,
      proto::Activity{
          .player = 3, .kind = proto::ActivityKind::Remote, .entry = camera, .traffic = 4},
      kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(1,
                 proto::Activity{.player = 3,
                                 .kind = proto::ActivityKind::Remote,
                                 .flags = proto::kActivityAnchored,
                                 .entry = camera},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(
      1,
      proto::Activity{.player = 3, .kind = proto::ActivityKind::Workspot, .view = {1.f, 0.f, 0.f}},
      kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(1,
                 proto::Activity{.player = 3,
                                 .kind = proto::ActivityKind::Remote,
                                 .entry = camera,
                                 .view = {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.not-finite"});
  // Host: a guest says its own, never another's.
  auto host = Host();
  host.Received(2,
                proto::Activity{.player = 2, .kind = proto::ActivityKind::Remote, .entry = camera},
                kSecond);
  CHECK(Rules(host).empty());
}

TEST_CASE("referee: a device's fire is the host's turret's, what a copy sees a guest's") {
  using Kind = proto::DeviceFireKind;
  auto host = Host();
  // A guest says its copy of a turret has its own V, and what broke in its game.
  host.Received(2, proto::DeviceFire{.entity = 7, .kind = Kind::Seen, .player = 2}, kSecond);
  host.Received(2, proto::DeviceFire{.entity = 7, .kind = Kind::Lost}, kSecond);
  host.Received(2, proto::Fracture{.player = 2, .at = {1.f, 2.f, 3.f}}, kSecond);
  CHECK(Rules(host).empty());
  // Never what the turret shoots at, nor in another player's name.
  host.Received(2, proto::DeviceFire{.entity = 7, .kind = Kind::Shot, .timeUs = 1}, kSecond);
  host.Received(2, proto::DeviceFire{.entity = 7, .kind = Kind::Aim}, kSecond);
  host.Received(2, proto::DeviceFire{.entity = 7, .kind = Kind::Seen, .player = 3}, kSecond);
  host.Received(2, proto::Fracture{.player = 3}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.host-only", "authority.host-only",
                                                "authority.not-yours", "authority.not-yours"});
  host.Received(
      2, proto::Fracture{.player = 2, .at = {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}},
      kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"value.not-finite"});

  auto guest = Guest();
  const proto::TargetRef us{proto::TargetKind::Player, 2};
  guest.Received(1, proto::DeviceFire{.entity = 7, .kind = Kind::Aim, .target = us}, kSecond);
  guest.Received(
      1, proto::DeviceFire{.entity = 7, .kind = Kind::Shot, .target = us, .timeUs = kSecond},
      kSecond);
  guest.Received(1, proto::DeviceFire{.entity = 7, .kind = Kind::Idle}, kSecond);
  guest.Received(1, proto::Fracture{.player = 3}, kSecond);
  CHECK(Rules(guest).empty());
  // The host never passes on what a copy sees, a turret is named, a shot
  // stamped, its target a player of the session. Our own break comes back
  // (the session's record of breaks: devices.md), and is no fault.
  guest.Received(1, proto::DeviceFire{.entity = 7, .kind = Kind::Seen, .player = 3}, kSecond);
  guest.Received(1, proto::DeviceFire{.kind = Kind::Aim}, kSecond);
  guest.Received(1, proto::DeviceFire{.entity = 7, .kind = Kind::Shot}, kSecond);
  guest.Received(
      1,
      proto::DeviceFire{.entity = 7, .kind = Kind::Aim, .target = {proto::TargetKind::Player, 6}},
      kSecond);
  guest.Received(1, proto::Fracture{.player = 2}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.guest-only", "value.range",
                                                 "value.range", "ids.unknown-player"});
}

TEST_CASE("referee: a turret in a player's hands is that player's word") {
  auto host = Host();
  host.Received(2, proto::TurretHands{.player = 2, .entity = 7, .at = {1.f, 2.f, 3.f}}, kSecond);
  CHECK(Rules(host).empty());
  // Not in another's name, named, a real place.
  host.Received(2, proto::TurretHands{.player = 3, .entity = 7}, kSecond);
  host.Received(2, proto::TurretHands{.player = 2}, kSecond);
  host.Received(2,
                proto::TurretHands{.player = 2,
                                   .entity = 7,
                                   .at = {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}},
                kSecond);
  CHECK(Rules(host) ==
        std::vector<std::string>{"authority.not-yours", "value.range", "value.not-finite"});

  auto guest = Guest();
  guest.Received(1, proto::TurretHands{.player = 3, .entity = 7, .flags = proto::kHandsFiring},
                 kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1, proto::TurretHands{.player = 2, .entity = 7}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.own-state-back"});
}

TEST_CASE("referee: a turret's refusal is the host's, for a claim of ours; a refused guest lets "
          "go") {
  // Guest: refused a turret we claimed, held by another player.
  auto guest = Guest();
  guest.Sent(1, proto::TurretHands{.entity = 7}, kSecond);
  guest.Received(1, proto::TurretBusy{.entity = 7, .holder = 3}, kSecond);
  CHECK(Rules(guest).empty());
  // Never claimed, held by nobody or by us, unnamed.
  guest.Received(1, proto::TurretBusy{.entity = 8, .holder = 3}, kSecond);
  guest.Received(1, proto::TurretBusy{.entity = 7}, kSecond);
  guest.Received(1, proto::TurretBusy{.entity = 7, .holder = 2}, kSecond);
  guest.Received(1, proto::TurretBusy{.holder = 3}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"reply.unasked", "value.range",
                                                 "authority.own-state-back", "value.range",
                                                 "reply.unasked"});

  // Host: a guest may not say it.
  auto host = Host();
  host.Received(2, proto::TurretBusy{.entity = 7, .holder = 3}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.host-only"});
  // Refused twice (it let go late): fine. Refused a third time in a row, it
  // never let go.
  host.Sent(2, proto::TurretBusy{.entity = 7, .holder = 3}, kSecond);
  host.Sent(2, proto::TurretBusy{.entity = 7, .holder = 3}, 2 * kSecond);
  CHECK(Rules(host).empty());
  host.Sent(2, proto::TurretBusy{.entity = 7, .holder = 3}, 3 * kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.turret-taken"});
  // A claim again after a pause is a new one.
  host.Sent(2, proto::TurretBusy{.entity = 7, .holder = 3}, 10 * kSecond);
  host.Sent(2, proto::TurretBusy{.entity = 7, .holder = 3}, 11 * kSecond);
  CHECK(Rules(host).empty());
}

TEST_CASE("referee: a scripted moment is the host's V's, spectating a guest's") {
  auto host = Host();
  host.Received(2, proto::StoryState{.player = 2, .tier = 1, .flags = proto::kStorySpectating},
                kSecond);
  CHECK(Rules(host).empty());
  host.Received(2, proto::StoryState{.player = 2, .tier = 2, .flags = proto::kStoryMoment},
                kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.host-only"});

  auto guest = Guest();
  guest.Received(1, proto::StoryState{.player = 1, .tier = 2, .flags = proto::kStoryMoment},
                 kSecond);
  guest.Received(1, proto::StoryState{.player = 3, .tier = 1, .flags = proto::kStorySpectating},
                 kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1, proto::StoryState{.player = 3, .tier = 2, .flags = proto::kStoryMoment},
                 kSecond);
  guest.Received(1, proto::StoryState{.player = 1, .tier = 1, .flags = proto::kStorySpectating},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.host-only", "value.range"});

  // A moment to watch is the host's word too, and always a moment.
  auto watch = Guest();
  watch.Received(
      1,
      proto::StoryState{.player = 1, .tier = 5, .flags = proto::kStoryMoment | proto::kStoryWatch},
      kSecond);
  CHECK(Rules(watch).empty());
  watch.Received(1, proto::StoryState{.player = 1, .tier = 5, .flags = proto::kStoryWatch},
                 kSecond);
  CHECK(Rules(watch) == std::vector<std::string>{"value.range"});
  auto told = Host();
  told.Received(
      2,
      proto::StoryState{.player = 2, .tier = 5, .flags = proto::kStoryMoment | proto::kStoryWatch},
      kSecond);
  CHECK(Rules(told) == std::vector<std::string>{"authority.host-only"});
}

TEST_CASE("referee: fast travel blocked by the quest is the host's word") {
  auto guest = Guest();
  guest.Received(1, proto::StoryState{.player = 1, .tier = 1, .flags = proto::kStoryNoFastTravel},
                 kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1, proto::StoryState{.player = 3, .tier = 1, .flags = proto::kStoryNoFastTravel},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.host-only"});

  auto host = Host();
  host.Received(2, proto::StoryState{.player = 2, .tier = 1, .flags = proto::kStoryNoFastTravel},
                kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.host-only"});
}

TEST_CASE("referee: a link request not taken goes to its asker only, in its decliner's name") {
  auto guest = Guest();
  guest.Received(1, proto::LinkDecline{.from = 3, .to = 2, .reason = proto::kLinkDeclineOther},
                 kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1, proto::LinkDecline{.from = 1, .to = 3, .reason = proto::kLinkDeclineOther},
                 kSecond);
  guest.Received(1, proto::LinkDecline{.from = 2, .to = 2, .reason = proto::kLinkDeclineOther},
                 kSecond);
  guest.Received(1, proto::LinkDecline{.from = 3, .to = 2, .reason = 9}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.misdelivered",
                                                 "authority.own-state-back", "value.range"});

  auto host = Host();
  host.Received(2, proto::LinkDecline{.to = 3, .reason = proto::kLinkDeclineOther}, kSecond);
  CHECK(Rules(host).empty());
  host.Received(2, proto::LinkDecline{.from = 3, .to = 1, .reason = proto::kLinkDeclineOther},
                kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.not-yours"});
}

TEST_CASE("referee: world prefabs are the host's world, story gates each player's own") {
  // A guest may not speak for the host's world.
  auto host = Host();
  host.Received(2, proto::WorldPrefabs{.player = 2, .prefabs = {0x10}}, kSecond);
  CHECK(Broke(host, "authority.host-only"));
  host.Received(2, proto::StoryGates{.player = 2, .flags = proto::kGateInWatson}, kSecond);
  CHECK(Rules(host).empty());

  auto guest = Guest();
  guest.Received(1, proto::WorldPrefabs{.player = 1, .prefabs = {0x10, 0x20}}, kSecond);
  guest.Received(1, proto::StoryGates{.player = 1, .flags = proto::kGateWatsonOpen}, kSecond);
  guest.Received(1, proto::StoryGates{.player = 3, .flags = proto::kGateInDogtown}, kSecond);
  CHECK(Rules(guest).empty());
  // Said to be another player's world; our own gates sent back to us.
  guest.Received(1, proto::WorldPrefabs{.player = 3, .prefabs = {0x10}}, kSecond);
  CHECK(Broke(guest, "authority.host-only"));
  guest.Received(1, proto::StoryGates{.player = 2, .flags = 0}, kSecond);
  CHECK(Broke(guest, "authority.own-state-back"));
}

TEST_CASE("referee: the host may not say what only a guest asks") {
  auto referee = Guest();
  referee.Received(1, proto::ResyncRequest{1}, kSecond);
  referee.Received(1, proto::DeviceUse{}, kSecond);
  CHECK(Rules(referee) == std::vector<std::string>(2, "authority.guest-only"));
  // A request comes to us only for an NPC our game runs.
  referee.Received(1, proto::NpcStrike{.npc = 1}, kSecond);
  CHECK(Rules(referee) == std::vector<std::string>{"authority.not-owner"});
  referee.Sent(1, proto::NpcSpawn{.npc = regions::MakeNpcId(2, 1)}, kSecond);
  referee.Received(1, proto::NpcStrike{.npc = regions::MakeNpcId(2, 1)}, kSecond);
  referee.Received(1, proto::NpcHealth{.npc = regions::MakeNpcId(2, 1)}, kSecond);
  CHECK(Rules(referee) == std::vector<std::string>{"authority.not-owner"});
  // Our own state never comes back; what is another's is never ours to get.
  referee.Received(1, proto::Held{.player = 2}, kSecond);
  referee.Received(1, proto::PlayerHit{.player = 3}, kSecond);
  referee.Received(1, proto::Revive{.player = 3, .target = 3}, kSecond);
  CHECK(Rules(referee) == std::vector<std::string>{"authority.own-state-back",
                                                   "authority.misdelivered",
                                                   "authority.misdelivered"});
  // "You are no longer paused" goes to everyone when a player returns.
  referee.Received(1, proto::Pause{2, false}, kSecond);
  referee.Received(1, proto::TimeEffect{.player = 2}, kSecond);
  referee.Received(1, proto::Held{.player = 3}, kSecond);
  CHECK(Rules(referee).empty());
  referee.Received(1, proto::Welcome{.you = 2, .host = 1}, kSecond);
  CHECK(Broke(referee, "oneshot.welcome-twice"));
}

TEST_CASE("referee: a weak spot's share and a trace's end go to the NPC's owner, sane") {
  auto host = Host();
  host.Sent(0, proto::NpcSpawn{.npc = 1, .record = 1}, kSecond);
  host.Received(2, proto::WeakSpotHit{.player = 2, .npc = 1, .record = 9, .drain = 30.f}, kSecond);
  host.Received(2, proto::HackTraced{.player = 2, .npc = 1}, kSecond);
  CHECK(Rules(host).empty());
  // In nobody else's name; a named weak spot, a share of its health.
  host.Received(2, proto::HackTraced{.player = 3, .npc = 1}, kSecond);
  host.Received(2, proto::WeakSpotHit{.player = 2, .npc = 1, .drain = 30.f}, kSecond);
  host.Received(2, proto::WeakSpotHit{.player = 2, .npc = 1, .record = 9, .drain = 0.f}, kSecond);
  host.Received(
      2,
      proto::WeakSpotHit{
          .player = 2, .npc = 1, .record = 9, .drain = std::numeric_limits<float>::quiet_NaN()},
      kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.not-yours", "value.range", "value.range",
                                                "value.not-finite"});
  // Passed to a guest only for an NPC its game runs.
  auto guest = Guest();
  guest.Received(1, proto::HackTraced{.player = 3, .npc = 1}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.not-owner"});
  guest.Sent(1, proto::NpcSpawn{.npc = regions::MakeNpcId(2, 1)}, kSecond);
  guest.Received(
      1,
      proto::WeakSpotHit{.player = 3, .npc = regions::MakeNpcId(2, 1), .record = 9, .drain = 5.f},
      kSecond);
  CHECK(Rules(guest).empty());
  // An owner's health of an NPC with a flag this version does not know.
  guest.Received(1, proto::NpcSpawn{.npc = 1}, kSecond);
  guest.Received(1, proto::NpcHealth{.npc = 1, .health = 0.5f, .flags = proto::kNpcProtected},
                 kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1, proto::NpcHealth{.npc = 1, .health = 0.5f, .flags = 2}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
}

TEST_CASE("referee: ids nobody announced, ids used twice") {
  SUBCASE("a guest names an NPC the host never shared") {
    auto referee = Host();
    referee.Sent(0, proto::NpcSpawn{.npc = 5, .record = 10}, kSecond);
    referee.Received(2, proto::NpcStrike{.npc = 5, .damage = 10.f}, kSecond);
    CHECK(Rules(referee).empty());
    referee.Received(2, proto::NpcStrike{.npc = 6, .damage = 10.f}, kSecond);
    referee.Received(2, proto::NpcAction{.npc = 99, .kind = proto::NpcActionKind::Grab}, kSecond);
    CHECK(Rules(referee) == std::vector<std::string>(2, "ids.unknown-npc"));
    // An NPC that died is still a name both know (a late hit, a body).
    referee.Sent(0, proto::NpcGone{.npc = 5, .reason = proto::kGoneDead}, kSecond);
    referee.Received(2, proto::NpcAction{.npc = 5, .kind = proto::NpcActionKind::PickUp}, kSecond);
    CHECK(Rules(referee).empty());
    referee.Received(2, proto::Revive{.target = 9}, kSecond);
    referee.Received(2, proto::WorldItemTaken{.id = 4}, kSecond);
    CHECK(Rules(referee) == std::vector<std::string>{"ids.unknown-player", "ids.unknown-item"});
  }
  SUBCASE("the host reuses an NPC id, or talks of one it never announced") {
    auto referee = Guest();
    referee.Received(1, proto::NpcSpawn{.npc = 5, .record = 10}, kSecond);
    referee.Received(1, proto::NpcSpawn{.npc = 5, .record = 10}, kSecond); // a resync
    referee.Received(1, proto::NpcHealth{.npc = 5, .health = 0.5f}, kSecond);
    CHECK(Rules(referee).empty());
    referee.Received(1, proto::NpcSpawn{.npc = 5, .record = 11}, kSecond);
    CHECK(Broke(referee, "ids.reused"));
    // Out of view and back (a link broke and formed again) is the same NPC.
    referee.Received(1, proto::NpcGone{.npc = 5, .reason = proto::kGoneAway}, kSecond);
    referee.Received(1, proto::NpcSpawn{.npc = 5, .record = 11}, kSecond);
    CHECK(Rules(referee).empty());
    referee.Received(1, proto::NpcGone{.npc = 5, .reason = proto::kGoneDead, .killer = 3}, kSecond);
    referee.Received(1, proto::NpcSpawn{.npc = 5, .record = 10}, kSecond);
    CHECK(Broke(referee, "ids.reused"));
    referee.Received(1, proto::NpcHealth{.npc = 8, .health = 0.5f}, kSecond);
    referee.Received(1, proto::NpcCue{.npc = 8, .player = 2, .cue = proto::NpcCueKind::CombatStart},
                     kSecond);
    referee.Received(1, proto::Held{.player = 7}, kSecond);
    CHECK(Rules(referee) ==
          std::vector<std::string>{"ids.unknown-npc", "ids.unknown-npc", "ids.unknown-player"});
    // Unreliable states may overtake the spawn they are about.
    referee.Received(1, proto::NpcStates{.seq = 1, .npcs = {{.npc = 12}}}, kSecond);
    referee.Received(1, proto::PlayerState{.player = 4, .seq = 1}, kSecond);
    CHECK(Rules(referee).empty());
  }
}

TEST_CASE("referee: one-shots that come twice") {
  auto referee = Guest();
  referee.Received(1, proto::QuestReward{.reward = 5, .quest = 9}, kSecond);
  referee.Received(1, proto::QuestReward{.reward = 5, .quest = 10}, kSecond);
  CHECK(Rules(referee).empty());
  referee.Received(1, proto::QuestReward{.reward = 5, .quest = 9}, 2 * kSecond);
  CHECK(Broke(referee, "oneshot.reward-twice"));

  referee.Received(1, proto::NpcSpawn{.npc = 5}, kSecond);
  referee.Received(1, proto::NpcGone{.npc = 5, .reason = proto::kGoneKnockedOut}, kSecond);
  referee.Received(1, proto::NpcGone{.npc = 5, .reason = proto::kGoneDead}, kSecond);
  referee.Received(1, proto::NpcGone{.npc = 5, .reason = proto::kGoneAway}, kSecond);
  CHECK(Rules(referee).empty());
  referee.Received(1, proto::NpcGone{.npc = 5, .reason = proto::kGoneDead}, kSecond);
  CHECK(Broke(referee, "oneshot.death-twice"));

  referee.Received(1, proto::GameOver{1}, kSecond);
  referee.Received(1, proto::GameOver{1}, kSecond);
  CHECK(Broke(referee, "oneshot.gameover-twice"));

  // A state sent twice, or numbered backwards (ENet never delivers an older one).
  referee.Received(1, proto::PlayerState{.player = 3, .seq = 10}, kSecond);
  referee.Received(1, proto::PlayerState{.player = 3, .seq = 11}, kSecond);
  CHECK(Rules(referee).empty());
  referee.Received(1, proto::PlayerState{.player = 3, .seq = 11}, kSecond);
  CHECK(Broke(referee, "oneshot.seq-repeated"));

  // One stack, two takers.
  referee.Received(1, proto::WorldItem{.id = 3, .dropper = 3, .stacks = {{1, 0, 1}}}, kSecond);
  referee.Received(1, proto::WorldItemTaken{.id = 3, .stack = 0, .taker = 3}, kSecond);
  referee.Received(1, proto::WorldItemTaken{.id = 3, .stack = 0, .taker = 3}, kSecond);
  CHECK(Rules(referee).empty());
  referee.Received(1, proto::WorldItemTaken{.id = 3, .stack = 0, .taker = 1}, kSecond);
  CHECK(Broke(referee, "oneshot.item-taken-twice"));

  // The same hit, byte for byte, at once: a hint only (two pellets may match).
  const proto::PlayerHit hit{.player = 2, .npc = 5, .physical = 10.f};
  referee.Received(1, hit, 3 * kSecond);
  referee.Received(1, hit, 3 * kSecond + 1000);
  CHECK(Rules(referee).empty());
  CHECK(referee.Counts().at("oneshot.duplicate") == 1);

  auto host = Host();
  host.Received(2, proto::WorldItem{.token = 4, .stacks = {{1, 0, 1}}}, kSecond);
  host.Received(2, proto::WorldItem{.token = 5, .stacks = {{1, 0, 1}}}, kSecond);
  CHECK(Rules(host).empty());
  host.Received(2, proto::WorldItem{.token = 4, .stacks = {{1, 0, 1}}}, kSecond);
  CHECK(Broke(host, "oneshot.drop-twice"));
}

TEST_CASE("referee: answers nobody asked for") {
  auto referee = Guest();
  referee.Received(1, proto::NpcSpawn{.npc = 5}, kSecond);
  referee.Sent(1, proto::NpcAction{.npc = 5, .kind = proto::NpcActionKind::Grab}, kSecond);
  referee.Received(1, proto::NpcActionReply{.npc = 5, .kind = proto::NpcActionKind::Grab}, kSecond);
  CHECK(Rules(referee).empty());
  referee.Received(1, proto::NpcActionReply{.npc = 5, .kind = proto::NpcActionKind::Grab}, kSecond);
  referee.Received(1, proto::WorldItem{.id = 3, .dropper = 3, .stacks = {{1, 0, 1}}}, kSecond);
  referee.Received(1, proto::WorldItemTaken{.id = 3, .stack = 0, .taker = 2}, kSecond);
  referee.Received(1, proto::Pong{.pingSentUs = 77}, kSecond);
  CHECK(Rules(referee) == std::vector<std::string>(3, "reply.unasked"));

  // A grab on an NPC the host said dead must be refused.
  referee.Received(1, proto::NpcGone{.npc = 5, .reason = proto::kGoneDead}, kSecond);
  referee.Sent(1, proto::NpcAction{.npc = 5, .kind = proto::NpcActionKind::Grab}, 3 * kSecond);
  referee.Received(1,
                   proto::NpcActionReply{.npc = 5,
                                         .kind = proto::NpcActionKind::Grab,
                                         .accepted = false,
                                         .reason = proto::kRefusedGone},
                   3 * kSecond);
  CHECK(Rules(referee).empty());
  referee.Sent(1, proto::NpcAction{.npc = 5, .kind = proto::NpcActionKind::Grab}, 4 * kSecond);
  referee.Received(
      1, proto::NpcActionReply{.npc = 5, .kind = proto::NpcActionKind::Grab, .accepted = true},
      4 * kSecond);
  CHECK(Broke(referee, "authority.accepted-on-dead"));
}

TEST_CASE("referee: epochs") {
  auto host = Host();
  // Our world is rebuilt: each guest owes its word that it saw it.
  host.Sent(0,
            proto::Epoch{.player = 1, .epoch = 2, .seenHost = 2, .reason = proto::kEpochFastTravel},
            kSecond);
  host.Received(2, proto::Epoch{.epoch = 1, .seenHost = 2}, kSecond + 1000);
  host.Tick(10 * kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"epoch.unacknowledged"});
  CHECK(host.Counts().at("epoch.unacknowledged") == 1); // player 3 only
  host.Received(2, proto::Epoch{.epoch = 3, .seenHost = 2, .reason = proto::kEpochFastTravel},
                11 * kSecond);
  host.Received(2, proto::Epoch{.epoch = 2, .seenHost = 2, .reason = proto::kEpochFastTravel},
                12 * kSecond);
  host.Received(2, proto::Epoch{.epoch = 2, .seenHost = 5}, 12 * kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"epoch.backwards", "epoch.unknown"});

  auto guest = Guest();
  guest.Received(1, proto::Epoch{.player = 1, .epoch = 3}, kSecond);
  guest.Received(1, proto::Digest{.epoch = 2}, kSecond);
  CHECK(Broke(guest, "epoch.backwards"));
}

TEST_CASE("referee: rates, sizes and values") {
  auto referee = Host();
  std::uint64_t now = kSecond;
  // 30 states a second: within budget.
  for (std::uint32_t i = 1; i <= 150; ++i, now += kSecond / 30)
    referee.Received(2, proto::PlayerState{.seq = i}, now, now);
  CHECK(Rules(referee).empty());
  // 300 a second: over it, said once.
  for (std::uint32_t i = 200; i <= 1200; ++i, now += kSecond / 300)
    referee.Received(2, proto::PlayerState{.seq = i}, now, now);
  CHECK(Rules(referee) == std::vector<std::string>{"rate.over-budget"});

  const float nan = std::numeric_limits<float>::quiet_NaN();
  referee.Received(3, proto::PlayerState{.seq = 1, .position = {nan, 0.f, 0.f}}, now, now);
  referee.Received(3, proto::NpcStrike{.npc = 1, .damage = -5.f}, now);
  CHECK(Rules(referee) ==
        std::vector<std::string>{"value.not-finite", "value.range", "ids.unknown-npc"});
  // A strike says known flags only (kStrikeLeftHand, v16: the off hand).
  referee.Received(3, proto::NpcStrike{.npc = 1, .damage = 5.f, .flags = proto::kStrikeLeftHand},
                   now);
  CHECK_FALSE(Broke(referee, "value.range"));
  referee.Received(3, proto::NpcStrike{.npc = 1, .damage = 5.f, .flags = 1024}, now);
  CHECK(Broke(referee, "value.range"));
  // A state made on another clock.
  referee.Received(3, proto::PlayerState{.seq = 2, .timeUs = now - 9 * kSecond}, now, now);
  CHECK(Broke(referee, "value.clock"));

  for (int i = 0; i < 7; ++i)
    referee.Received(3, proto::ResyncRequest{proto::kDomainNpcs}, now + i * 1000);
  CHECK(referee.Counts().at("rate.over-budget") == 2);
  CHECK(referee.Counts().at("digest.resync-loop") == 1);

  auto guest = Guest();
  guest.Received(1, proto::NpcSpawn{.npc = 1}, kSecond);
  guest.Received(1, proto::NpcHealth{.npc = 1, .health = 1.5f}, kSecond);
  CHECK(Broke(guest, "value.range"));

  // A smart gun's guided round says what it is in known flags only (v13).
  guest.Received(1,
                 proto::NpcShotAt{.player = 2,
                                  .npc = 1,
                                  .timeUs = kSecond,
                                  .flags = proto::kShotAtSmart | proto::kShotAtTracked},
                 kSecond, kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1, proto::NpcShotAt{.player = 2, .npc = 1, .timeUs = kSecond + 1, .flags = 8},
                 kSecond, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  // A V's ricochet and its charge: a real place, a known step.
  guest.Received(1, proto::Cue{.player = 3, .seq = 1, .kind = proto::CueKind::Ricochet}, kSecond);
  guest.Received(1,
                 proto::Cue{.player = 3,
                            .seq = 2,
                            .kind = proto::CueKind::Charge,
                            .arg = proto::kChargeFull,
                            .at = {1.f, 0.f, 0.f}},
                 kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1, proto::Cue{.player = 3, .seq = 3, .kind = proto::CueKind::Charge, .arg = 9},
                 kSecond);
  guest.Received(
      1, proto::Cue{.player = 3, .seq = 4, .kind = proto::CueKind::Ricochet, .at = {nan, 0.f, 0.f}},
      kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range", "value.not-finite"});
  // Dual wielding (v16): a charge names its hand, the right or the left; the
  // left hand's hit is a strike with its flag, and no flag is unknown.
  guest.Received(1,
                 proto::Cue{.player = 3,
                            .seq = 5,
                            .kind = proto::CueKind::Charge,
                            .arg = proto::kChargeCharging,
                            .at = {0.5f, proto::kChargeLeftHand, 0.f}},
                 kSecond);
  guest.Received(1,
                 proto::Cue{.player = 3,
                            .seq = 6,
                            .kind = proto::CueKind::Trace,
                            .arg = proto::kTraceLeftHand,
                            .at = {1.f, 2.f, 3.f}},
                 kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1,
                 proto::Cue{.player = 3,
                            .seq = 7,
                            .kind = proto::CueKind::Charge,
                            .arg = proto::kChargeCharging,
                            .at = {0.5f, 2.f, 0.f}},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
}

TEST_CASE("referee: a hit a V took is that V's game's word, on its body, by someone known") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  // Guest: another player's V was hit by a shared NPC, by us, by nobody known.
  auto guest = Guest();
  guest.Received(1,
                 proto::HitTaken{.player = 3,
                                 .by = proto::HitBy::Npc,
                                 .attacker = 7,
                                 .attackType = 10,
                                 .zone = 8,
                                 .offset = {0.f, 0.f, 0.5f},
                                 .direction = {1.f, 0.f, 0.f}},
                 kSecond);
  guest.Received(1, proto::HitTaken{.player = 3, .by = proto::HitBy::Player, .attacker = 2},
                 kSecond);
  guest.Received(1, proto::HitTaken{.player = 1}, kSecond);
  CHECK(Rules(guest).empty());
  // Never our own back; a player never in the session; off the body; no real way.
  guest.Received(1, proto::HitTaken{.player = 2}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.own-state-back"});
  guest.Received(1, proto::HitTaken{.player = 3, .by = proto::HitBy::Player, .attacker = 6},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"ids.unknown-player"});
  guest.Received(1, proto::HitTaken{.player = 3, .offset = {0.f, 9.f, 0.f}}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(1, proto::HitTaken{.player = 3, .direction = {nan, 0.f, 0.f}}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.not-finite"});
  // An attacker named for nobody, a flag this version does not know.
  guest.Received(1, proto::HitTaken{.player = 3, .attacker = 7}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(1, proto::HitTaken{.player = 3, .flags = 16}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});

  // Host: a guest tells its own V's hits, in its own name only.
  auto host = Host();
  host.Received(2, proto::HitTaken{.by = proto::HitBy::Player, .attacker = 3}, kSecond);
  CHECK(Rules(host).empty());
  host.Received(2, proto::HitTaken{.player = 3}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.not-yours"});
}

TEST_CASE("referee: resync requests are budgeted at the pace of the endpoint's digests") {
  // A test's timings, a digest every 100 ms: a guest may ask after each one
  // (7 in 10 s with a third of everything lost was called over budget, the
  // game's 6, in regions_endpoint_test).
  const auto port = test::NextPort();
  std::string error;
  auto host =
      Endpoint::Host({"host"}, port, error, Timing{.digest = 100ms, .resyncCooldown = 200ms});
  REQUIRE_MESSAGE(host, error);
  Referee referee(true);
  referee.Attach(*host);
  referee.Sent(2, proto::Welcome{.you = 2, .host = 1}, kSecond);
  referee.Sent(0, proto::Roster{{{1, "us", {}}, {2, "a", {}}}}, kSecond);
  auto now = kSecond;
  for (int i = 0; i < 100; ++i, now += kSecond / 10)
    referee.Received(2, proto::ResyncRequest{proto::kDomainNpcs}, now);
  CHECK_FALSE(referee.Counts().contains("rate.over-budget"));
  // Ten times faster than its digests come: over it.
  for (int i = 0; i < 300; ++i, now += kSecond / 100)
    referee.Received(2, proto::ResyncRequest{proto::kDomainNpcs}, now);
  CHECK(referee.Counts().contains("rate.over-budget"));
}

TEST_CASE("referee: a V's noises and its throws' paths: in its own name, sane, not a flood") {
  auto referee = Host();
  std::uint64_t now = kSecond;
  // Footsteps four times a second and a grenade's path beside them: fine.
  for (int i = 0; i < 12; ++i, now += kSecond / 4) {
    referee.Received(2, proto::Noise{.stim = 26}, now);
    referee.Received(
        2, proto::ThrowPath{.id = 1, .position = {1.f, 2.f, 3.f}, .velocity = {0.f, 8.f, 2.f}},
        now);
  }
  CHECK(Rules(referee).empty());
  // In another player's name; a speed nothing thrown has; not a stimulus.
  referee.Received(2, proto::ThrowPath{.player = 3, .id = 1}, now);
  CHECK(Rules(referee) == std::vector<std::string>{"authority.not-yours"});
  referee.Received(3, proto::ThrowPath{.id = 1, .velocity = {0.f, 900.f, 0.f}}, now);
  referee.Received(3, proto::Noise{.stim = 99}, now);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  referee.Received(3, proto::ThrowPath{.id = 1, .position = {nan, 0.f, 0.f}}, now);
  CHECK(Rules(referee) ==
        std::vector<std::string>{"value.range", "value.range", "value.not-finite"});
  // A footstep every frame (ungated): over its budget, said once.
  for (int i = 0; i < 400; ++i, now += kSecond / 120)
    referee.Received(2, proto::Noise{.stim = 25}, now);
  CHECK(Rules(referee) == std::vector<std::string>{"rate.over-budget"});
}

TEST_CASE("referee: a waypoint, a tracked objective included, is its player's, a real place") {
  using Kind = proto::WaypointKind;
  auto host = Host();
  host.Received(2, proto::Waypoint{.player = 2, .kind = Kind::Objective, .position = {1, 2, 3}},
                kSecond);
  host.Received(3, proto::Waypoint{.kind = Kind::Pin, .position = {4, 5, 6}}, kSecond);
  host.Received(3, proto::Waypoint{.player = 3}, kSecond);
  CHECK(Rules(host).empty());
  // Not in another's name, a kind this version knows, a place that is one.
  host.Received(2, proto::Waypoint{.player = 3, .kind = Kind::Objective}, kSecond);
  host.Received(2, proto::Waypoint{.kind = static_cast<Kind>(4)}, kSecond);
  host.Received(2,
                proto::Waypoint{.kind = Kind::Objective,
                                .position = {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}},
                kSecond);
  CHECK(Rules(host) ==
        std::vector<std::string>{"authority.not-yours", "value.range", "value.not-finite"});

  // The host's own objective and a guest's, relayed: anyone's may be shared.
  auto guest = Guest();
  guest.Received(1, proto::Waypoint{.player = 1, .kind = Kind::Objective, .position = {1, 2, 3}},
                 kSecond);
  guest.Received(1, proto::Waypoint{.player = 3, .kind = Kind::Objective, .position = {7, 8, 9}},
                 kSecond);
  CHECK(Rules(guest).empty());
}

TEST_CASE("referee: states held and never released") {
  auto guest = Guest();
  guest.Received(1, proto::NpcSpawn{.npc = 1}, kSecond);
  guest.Received(1, proto::NpcSpawn{.npc = 2}, kSecond);
  guest.Received(1, proto::NpcHold{.npc = 1, .player = 3, .how = proto::NpcHoldKind::Takedown},
                 kSecond);
  guest.Received(1, proto::NpcHold{.npc = 2, .player = 3, .how = proto::NpcHoldKind::Carried},
                 kSecond);
  guest.Received(
      1, proto::TimeEffect{.player = 1, .abilities = proto::kTimeSandevistan, .scale = 0.3f},
      kSecond);
  guest.Tick(20 * kSecond);
  CHECK(Rules(guest).empty());
  guest.Tick(40 * kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"stuck.hold"});
  // The holder leaves: the host frees what it held at once.
  guest.Received(1, proto::Roster{{{1, "host", {}}, {2, "us", {}}}}, 50 * kSecond);
  guest.Tick(51 * kSecond);
  CHECK(Rules(guest).empty());
  guest.Tick(53 * kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"stuck.after-leave"}); // the carried body
  guest.Tick(200 * kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"stuck.time-effect"});
  // Ended in time: nothing.
  guest.Received(
      1, proto::TimeEffect{.player = 3, .abilities = proto::kTimeSandevistan, .scale = 0.3f},
      200 * kSecond);
  guest.Received(1, proto::TimeEffect{.player = 3}, 210 * kSecond);
  guest.Tick(500 * kSecond);
  CHECK(Rules(guest).empty());

  auto host = Host();
  host.Received(2, proto::Vitals{.life = proto::Life::Down}, kSecond);
  host.Received(3, proto::Revive{.target = 2, .step = proto::ReviveStep::Start}, 2 * kSecond);
  host.Tick(20 * kSecond);
  CHECK(Rules(host).empty());
  host.Tick(40 * kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"stuck.revive"});
  // A V that is down holds no weapon; one that is up is not revived.
  host.Received(2, proto::Shot{.seq = 1, .effect = 16}, 41 * kSecond);
  host.Received(3, proto::Revive{.target = 1, .step = proto::ReviveStep::Start}, 41 * kSecond);
  CHECK(Rules(host) ==
        std::vector<std::string>{"authority.acting-while-down", "authority.revive-not-down"});
}

TEST_CASE("referee: a vehicle moved from someone else's seat") {
  auto host = Host();
  host.Received(2, proto::Seat{.vehicle = 9, .seat = 1}, kSecond); // its own car
  host.Received(2, proto::VehicleState{.seq = 1}, 5 * kSecond);
  CHECK(Rules(host).empty());
  host.Received(2, proto::Seat{.vehicle = 9, .seat = 2, .owner = 3}, 6 * kSecond);
  host.Received(2, proto::VehicleState{.seq = 2}, 6 * kSecond + 1000); // still in flight
  CHECK(Rules(host).empty());
  host.Received(2, proto::VehicleState{.seq = 3}, 9 * kSecond);
  CHECK(Broke(host, "authority.vehicle-not-yours"));
}

TEST_CASE("referee: digests that never agree again") {
  auto guest = Guest(true);
  proto::Digest hosts{.epoch = 1, .npcs = 5}, ours{.epoch = 1, .npcs = 6};
  for (std::uint64_t t = 1; t <= 18; t += 3)
    guest.Received(1, hosts, t * kSecond, 0, &ours);
  CHECK(Rules(guest).empty());
  // Healed, then apart again: the clock starts anew.
  guest.Received(1, hosts, 20 * kSecond, 0, &hosts);
  for (std::uint64_t t = 21; t <= 39; t += 3)
    guest.Received(1, hosts, t * kSecond, 0, &ours);
  CHECK(Rules(guest).empty());
  guest.Received(1, hosts, 45 * kSecond, 0, &ours);
  CHECK(Rules(guest) == std::vector<std::string>{"digest.diverged"});
  // ...and a host that stops checking.
  guest.Tick(60 * kSecond);
  CHECK(Rules(guest).empty());
  guest.Tick(70 * kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"rate.digest-silent"});
}

TEST_CASE("referee: a clean session over loopback breaks no rule") {
  const auto port = test::NextPort();
  std::string error;
  const Timing fast{.digest = 200ms, .resyncCooldown = 400ms};
  auto host = Endpoint::Host({"host"}, port, error, fast);
  REQUIRE_MESSAGE(host, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error, fast);
  REQUIRE_MESSAGE(guest, error);
  Referee atHost(true), atGuest(false);
  atHost.Attach(*host);
  atGuest.Attach(*guest);

  const auto deadline = std::chrono::steady_clock::now() + 1500ms;
  auto nextSend = std::chrono::steady_clock::now();
  std::uint32_t tick = 0;
  bool spawned = false, asked = false;
  while (std::chrono::steady_clock::now() < deadline) {
    host->Update(1ms);
    guest->Update(1ms);
    atHost.Tick();
    atGuest.Tick();
    // 50 states a second, as a game sends them.
    if (!guest->IsJoined() || std::chrono::steady_clock::now() < nextSend)
      continue;
    nextSend += 20ms;
    ++tick;
    if (!spawned) {
      spawned = true;
      host->SetWorld({.gameTimeSeconds = 3600});
      host->SendNpcSpawn({.npc = 1, .record = 7});
      host->SendNpcHealth({.npc = 1, .health = 1.f});
      host->SetLocalHeld({.right = 5});
      guest->SetLocalVitals({});
    }
    host->SendLocalState({.position = {static_cast<float>(tick), 0.f, 0.f}});
    guest->SendLocalState({.position = {0.f, static_cast<float>(tick), 0.f}});
    if (tick % 5 == 0)
      host->SendNpcStates({.npcs = {{.npc = 1}}});
    if (tick == 20 && !asked) {
      asked = true;
      guest->SendCombat(proto::NpcStrike{.npc = 1, .damage = 10.f});
      guest->SendNpcAction({.npc = 1, .kind = proto::NpcActionKind::Grab});
      host->SendNpcActionReply(2, {.npc = 1, .kind = proto::NpcActionKind::Grab, .accepted = true});
      host->SendNpcHold({.npc = 1, .player = 2, .how = proto::NpcHoldKind::Grappled});
      host->BumpEpoch(proto::kEpochFastTravel);
    }
  }
  REQUIRE(asked);
  for (auto *referee : {&atHost, &atGuest})
    for (const auto &finding : referee->Take())
      FAIL_CHECK(finding.Line());
  CHECK(atHost.Violations() == 0);
  CHECK(atGuest.Violations() == 0);

  // What the run touched, and what it never did.
  sim::Coverage coverage;
  coverage.Declare("npc.grab");
  coverage.Declare("npc.throw");
  coverage.Hit("npc.grab");
  coverage.Messages(atHost.SentCounts(), atHost.ReceivedCounts());
  CHECK(coverage.FeaturesHit() == 1);
  CHECK(coverage.Missed() == std::vector<std::string>{"npc.throw"});
  const auto never = coverage.NeverSeen();
  CHECK(std::ranges::find(never, "NpcStrike") == never.end());
  CHECK(std::ranges::find(never, "QuestReward") != never.end());
  CHECK(coverage.Report().size() == 5);
}

TEST_CASE("referee: every message has its speaker, and the other side may not say it") {
  constexpr auto kinds = std::variant_size_v<proto::Message>;
  const auto message = [](std::size_t index) {
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      proto::Message out;
      ((index == I
            ? (out = proto::Message(std::variant_alternative_t<I, proto::MessageVariant>{}), 0)
            : 0),
       ...);
      return out;
    }(std::make_index_sequence<kinds>{});
  };
  std::size_t hosts = 0, guests = 0;
  for (std::size_t i = 0; i < kinds; ++i) {
    CAPTURE(sim::MessageName(i));
    if (sim::SpeakerOf(i) == sim::Speaker::Host) {
      ++hosts;
      auto referee = Host();
      referee.Received(2, message(i), kSecond);
      CHECK(Broke(referee, "authority.host-only"));
    } else if (sim::SpeakerOf(i) == sim::Speaker::Guest) {
      ++guests;
      auto referee = Guest();
      referee.Received(1, message(i), kSecond);
      CHECK(Broke(referee, "authority.guest-only"));
    }
  }
  CHECK(hosts > 0);
  CHECK(guests > 0);
}

TEST_CASE("referee: an NPC seated in a shared world vehicle names the vehicle, not a player") {
  auto referee = Guest();
  referee.Received(1, proto::NpcSpawn{.npc = 7, .record = 1}, kSecond);
  referee.Received(
      1, proto::NpcSeat{.npc = 7, .owner = proto::TrafficSeatOwner(proto::MakeTrafficId(1, 1))},
      kSecond);
  CHECK(Rules(referee).empty());
  // Its area owner must still be a player of the session.
  referee.Received(
      1, proto::NpcSeat{.npc = 7, .owner = proto::TrafficSeatOwner(proto::MakeTrafficId(9, 1))},
      kSecond);
  CHECK(Rules(referee) == std::vector<std::string>{"ids.unknown-player"});
}

TEST_CASE("referee: a scene's props, camera and rig poses are the host's, about what it shares") {
  auto host = Host();
  host.Received(2, proto::SceneProp{.play = 1, .prop = 1, .record = 5, .holder = 7}, kSecond);
  host.Received(2, proto::SceneCamera{.play = 1, .fov = 60.f}, kSecond);
  host.Received(2, proto::RigPose{.npc = 7, .bones = 4}, kSecond);
  host.Received(2, proto::RigTracks{.npc = 7, .tracks = 4}, kSecond);
  host.Received(2, proto::ScenePlace{.play = 1, .prop = 2, .entity = 5}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>(5, "authority.host-only"));

  auto guest = Guest();
  guest.Received(1, proto::NpcSpawn{.npc = 7, .record = 10}, kSecond);
  guest.Received(1, proto::SceneProp{.play = 1, .prop = 1, .record = 5, .holder = 7}, kSecond);
  guest.Received(1, proto::SceneProp{.play = 1, .prop = 1, .record = 5, .holderPlayer = 3},
                 kSecond);
  guest.Received(1, proto::SceneProp{.play = 1, .prop = 1, .flags = proto::kPropGone}, kSecond);
  CHECK(Rules(guest).empty());
  // Held by an NPC nobody announced, by a player who never was.
  guest.Received(1, proto::SceneProp{.play = 1, .prop = 2, .record = 5, .holder = 99}, kSecond);
  guest.Received(1, proto::SceneProp{.play = 1, .prop = 3, .record = 5, .holderPlayer = 9},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"ids.unknown-npc", "ids.unknown-player"});
  // A view that is not a number.
  guest.Received(1,
                 proto::SceneCamera{.play = 1,
                                    .position = {std::numeric_limits<float>::infinity(), 0.f, 0.f},
                                    .fov = 60.f},
                 kSecond);
  CHECK(Broke(guest, "value.not-finite"));
}

TEST_CASE("referee: the death ledger is stamped by the host, reported by an NPC's owner") {
  // A guest hears deaths the host stamped: once each, the order never changing.
  auto guest = Guest();
  const proto::NpcDeath first{.entity = 0xA, .record = 1, .by = 3, .order = 1, .timeUs = 5};
  guest.Received(1, first, kSecond);
  guest.Received(1, first, 2 * kSecond); // a resync says the same again
  CHECK(Rules(guest).empty());
  auto moved = first;
  moved.order = 2;
  guest.Received(1, moved, 3 * kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"ledger.changed"});
  // Never an unstamped report passed on. One by a player that left before
  // we joined is fine.
  guest.Received(1, proto::NpcDeath{.npc = 7, .entity = 0xB}, 3 * kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.guest-only"});
  guest.Received(1, proto::NpcDeath{.entity = 0xC, .by = 9, .order = 3}, 3 * kSecond);
  CHECK(Rules(guest).empty());

  // The host hears reports: of an NPC the sender runs, never stamped.
  auto host = Host();
  const auto theirs = regions::MakeNpcId(2, 1);
  host.Received(2, proto::NpcSpawn{.npc = theirs, .record = 1}, kSecond);
  host.Received(2, proto::NpcPlaced{.npc = theirs, .entity = 0xA}, kSecond);
  host.Received(2, proto::NpcDeath{.npc = theirs, .entity = 0xA}, kSecond);
  CHECK(Rules(host).empty());
  host.Received(3, proto::NpcDeath{.npc = theirs, .entity = 0xA}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.not-owner"});
  host.Received(3, proto::NpcPlaced{.npc = theirs, .entity = 0xD}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.not-owner"});
  host.Received(2, first, kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.host-only"});
}

TEST_CASE("referee: a street race is described, seated, ordered and paid by its starter only") {
  // A guest hears its host's race and another guest's progress in it.
  auto guest = Guest();
  guest.Received(1,
                 proto::Race{.player = 1,
                             .starter = 1,
                             .race = 1,
                             .kind = proto::RaceKind::Info,
                             .arg = proto::kRaceGrid,
                             .checkpoints = 4},
                 kSecond);
  guest.Received(1,
                 proto::Race{.player = 3,
                             .starter = 1,
                             .race = 1,
                             .kind = proto::RaceKind::Progress,
                             .checkpoint = 2,
                             .metres = 40.f},
                 kSecond);
  CHECK(Rules(guest).empty());
  // Another player's word on the host's race order: not its to say.
  guest.Received(
      1, proto::Race{.player = 3, .starter = 1, .race = 1, .kind = proto::RaceKind::Standings},
      kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(
      1, proto::Race{.player = 1, .starter = 1, .kind = proto::RaceKind::Progress, .metres = -1.f},
      kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  // The route: the starter's, whole or none.
  guest.Received(1,
                 proto::Race{.player = 1,
                             .starter = 1,
                             .race = 1,
                             .kind = proto::RaceKind::Route,
                             .arg = proto::kRaceRewardsBadlands,
                             .checkpoints = 2,
                             .points = {{1.f, 2.f, 3.f}, {4.f, 5.f, 6.f}}},
                 kSecond);
  guest.Received(
      1,
      proto::Race{
          .player = 1, .starter = 1, .race = 1, .kind = proto::RaceKind::Route, .checkpoints = 4},
      kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1,
                 proto::Race{.player = 3,
                             .starter = 1,
                             .race = 1,
                             .kind = proto::RaceKind::Route,
                             .checkpoints = 1,
                             .points = {{1.f, 2.f, 3.f}}},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(1,
                 proto::Race{.player = 1,
                             .starter = 1,
                             .race = 1,
                             .kind = proto::RaceKind::Route,
                             .checkpoints = 3,
                             .points = {{1.f, 2.f, 3.f}}},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});

  // The host hears a guest join its race, in the guest's own name.
  auto host = Host();
  host.Received(2, proto::Race{.starter = 1, .race = 1, .kind = proto::RaceKind::Join, .arg = 1},
                kSecond);
  CHECK(Rules(host).empty());
  host.Received(2, proto::Race{.player = 3, .starter = 1, .kind = proto::RaceKind::Join, .arg = 1},
                kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.not-yours"});
  // A guest seating players in the host's race: refused.
  host.Received(2, proto::Race{.starter = 1, .to = 3, .kind = proto::RaceKind::Seat, .arg = 1},
                kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"value.range"});
}

TEST_CASE("referee: traffic lights are another game's word, never ours back") {
  auto guest = Guest();
  guest.Received(
      1, proto::TrafficLights{.owner = 3, .lights = {{.entity = 9, .color = proto::kLightRed}}},
      kSecond);
  guest.Received(1, proto::TrafficLights{.owner = 1, .full = true}, kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1, proto::TrafficLights{.owner = 2}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.own-state-back"});
  guest.Received(1, proto::TrafficLights{.owner = 9}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"ids.unknown-player"});
  guest.Received(1, proto::TrafficLights{.owner = 3, .lights = {{.entity = 0}}}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
}

TEST_CASE("referee: an NPC's voice line is its owner's word, about an NPC it shows us") {
  auto guest = Guest();
  guest.Received(1, proto::NpcSpawn{.npc = 7, .record = 1}, kSecond);
  guest.Received(1, proto::NpcVoice{.npc = 7, .context = 0x99}, kSecond);
  CHECK(Rules(guest).empty());
  // An NPC never announced; a line with no context.
  guest.Received(1, proto::NpcVoice{.npc = 8, .context = 0x99}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"ids.unknown-npc"});
  guest.Received(1, proto::NpcVoice{.npc = 7}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  // Gone from our view: its owner's lines are for the games that show it.
  guest.Received(1, proto::NpcGone{.npc = 7, .reason = proto::kGoneAway}, 2 * kSecond);
  guest.Received(1, proto::NpcVoice{.npc = 7, .context = 0x99}, 5 * kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.not-shared"});

  // A guest speaks for its own NPCs only, and no faster than a voice can.
  auto host = Host();
  const auto ours = regions::MakeNpcId(2, 1);
  host.Received(2, proto::NpcSpawn{.npc = ours, .record = 1}, kSecond);
  host.Received(2, proto::NpcVoice{.npc = ours, .context = 0x99}, kSecond);
  CHECK(Rules(host).empty());
  const auto theirs = regions::MakeNpcId(3, 1);
  host.Received(2, proto::NpcVoice{.npc = theirs, .context = 0x99}, kSecond);
  CHECK(Rules(host) == std::vector<std::string>{"authority.not-owner"});
  for (std::uint64_t i = 0; i < 200; ++i)
    host.Received(2, proto::NpcVoice{.npc = ours, .context = 0x99}, kSecond + i * 1000);
  CHECK(Broke(host, "rate.over-budget"));
}

TEST_CASE("referee: a body's rest is of an NPC we were told of, never ours back") {
  auto guest = Guest();
  guest.Received(1, proto::NpcSpawn{.npc = 7, .record = 1}, kSecond);
  guest.Received(1, proto::NpcGone{.npc = 7, .reason = proto::kGoneDead}, kSecond);
  const std::vector<proto::RagdollBody> bodies{
      {.offset = {0, 0, 10}, .rotation = {0, 0, 0, 32767}}};
  guest.Received(1, proto::CorpsePose{.npc = 7, .position = {1.f, 2.f, 3.f}, .bodies = bodies},
                 kSecond);
  CHECK(Rules(guest).empty());
  guest.Received(1, proto::CorpsePose{.npc = 8, .bodies = bodies}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"ids.unknown-npc"});
  guest.Received(1, proto::CorpsePose{.npc = 7, .yaw = std::nanf(""), .bodies = bodies}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.not-finite"});
  // No bodies, or a body turned by no rotation.
  guest.Received(1, proto::CorpsePose{.npc = 7}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(1, proto::CorpsePose{.npc = 7, .bodies = {{.offset = {1, 2, 3}}}}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  const auto ours = regions::MakeNpcId(2, 1);
  guest.Sent(1, proto::NpcSpawn{.npc = ours, .record = 2}, kSecond);
  guest.Received(1, proto::CorpsePose{.npc = ours, .bodies = bodies}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.own-state-back"});
}

TEST_CASE("referee: what a game runs without is its own word, passed on, never back") {
  auto guest = Guest();
  guest.Received(
      1,
      proto::Degraded{
          .player = 3,
          .entries = {{.id = "looks"}, {.id = "vehicle move", .flags = proto::kDegradedChosen}}},
      kSecond);
  guest.Received(1, proto::Degraded{.player = 3}, kSecond); // nothing off any more
  CHECK(Rules(guest).empty());
  // Our own back, a player nobody knows, ids out of order, a flag of no
  // version, an id the registry never writes.
  guest.Received(1, proto::Degraded{.player = 2}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"authority.own-state-back"});
  guest.Received(1, proto::Degraded{.player = 9}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"ids.unknown-player"});
  guest.Received(1,
                 proto::Degraded{.player = 3, .entries = {{.id = "vehicle move"}, {.id = "looks"}}},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(1, proto::Degraded{.player = 3, .entries = {{.id = "looks", .flags = 8}}},
                 kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  guest.Received(1, proto::Degraded{.player = 3, .entries = {{.id = "LOOKS"}}}, kSecond);
  CHECK(Rules(guest) == std::vector<std::string>{"value.range"});
  // Host: a guest says it in its own name.
  auto host = Host();
  host.Received(2, proto::Degraded{.player = 2, .entries = {{.id = "looks"}}}, kSecond);
  CHECK(Rules(host).empty());
}
