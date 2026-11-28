#include "tandem/consistency.hpp"
#include "tandem/protocol.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

using namespace tandem::proto;

namespace {
template <class T> void RoundTrips(const T &value) {
  const Message message = value;
  const auto bytes = Encode(message);
  REQUIRE_FALSE(bytes.empty());
  const auto decoded = Decode(bytes);
  REQUIRE(decoded.has_value());
  CHECK(std::get<T>(*decoded) == value);
}
} // namespace

TEST_CASE("every message survives encoding") {
  RoundTrips(Hello{.name = "V", .gameVersion = "2.50", .body = Body::Male});
  RoundTrips(Hello{.name = "V", .gameVersion = "0.2.0+2fa03ca", .flags = 0});
  CHECK(Hello{}.flags == kHelloExpansion);
  RoundTrips(Welcome{.you = 2, .host = 1, .hostTimeUs = 123456789});
  RoundTrips(Refused{"full"});
  RoundTrips(Ping{42});
  RoundTrips(Pong{42, 99});
  RoundTrips(Roster{{{1, "host", Body::Female}, {2, "guest", Body::Male}}});
  RoundTrips(PlayerState{.player = 2,
                         .seq = 7,
                         .timeUs = 1'000'000,
                         .position = {1.f, -2.f, 3.5f},
                         .orientation = {0.f, 0.f, 0.7071f, 0.7071f},
                         .velocity = {0.f, 4.f, 0.f},
                         .stance = 3,
                         .flags = 0x0102});
  RoundTrips(Bye{"bye"});
  PoseState pose{.player = 3, .seq = 9, .timeUs = 2'000'000, .hips = {1, -2, 8192}};
  pose.rotations[0] = 0xAB;
  pose.rotations.back() = 0x12;
  RoundTrips(pose);
  RoundTrips(Appearance{.player = 2,
                        .revision = 3,
                        .body = Body::Female,
                        .look = {{0x1111, 0x2222}, {0x3333, 0x4444}},
                        .items = {0x15e213846eULL}});
  RoundTrips(Held{.player = 2, .revision = 4, .right = 0x1234567890ULL, .left = 0});
  RoundTrips(Shot{.player = 2, .seq = 9, .hand = Hand::Left, .effect = 17});
  RoundTrips(
      Seat{.player = 2, .revision = 1, .vehicle = 0x2a1b, .seat = 0xb000b1d029d0cea0, .owner = 2});
  RoundTrips(VehicleState{.player = 2,
                          .seq = 3,
                          .timeUs = 5'000'000,
                          .position = {1.f, 2.f, 3.f},
                          .orientation = {0.f, 0.f, 0.38f, 0.92f},
                          .velocity = {10.f, 0.f, 0.f}});
  RoundTrips(
      NpcSpawn{.npc = 7, .record = 0x2a1b, .appearance = 0x33, .position = {1, 2, 3}, .yaw = 0.5f});
  RoundTrips(
      NpcStates{.seq = 2,
                .timeUs = 9,
                .npcs = {{.npc = 7, .position = {1, 2, 3}, .yaw = 1.f, .velocity = {1.5f, 0, 0}}}});
  RoundTrips(NpcGone{.npc = 7, .reason = 1, .killer = 2});
  RoundTrips(NpcHit{.player = 2,
                    .npc = 7,
                    .damage = 41.5f,
                    .flags = kHitWeakSpot | kHitLethal,
                    .position = {1, 2, 3},
                    .direction = {0, 1, 0}});
  RoundTrips(NpcEffect{.player = 2, .npc = 7, .effects = {11, 12}});
  RoundTrips(NpcHealth{.npc = 7, .health = 0.25f});
  RoundTrips(NpcHealth{.npc = 7, .health = 0.01f, .flags = kNpcProtected});
  RoundTrips(World{
      .revision = 2, .gameTimeSeconds = 86400 + 3600 * 18, .weather = 0x1234, .timePaused = true});
  RoundTrips(Pause{.player = 3, .paused = true});
  RoundTrips(Pause{.player = 2, .paused = false});
  RoundTrips(TimeEffect{.player = 2, .abilities = kTimeSandevistan, .scale = 0.25f});
  RoundTrips(NpcShotAt{.player = 2,
                       .npc = 7,
                       .timeUs = 123'456'789'012ULL,
                       .attack = 0x1122334455ULL,
                       .weapon = 0x0abcdef012ULL,
                       .tbh = 0.8f,
                       .trigger = 2,
                       .factor = 0.25f});
  // A smart gun's guided round (v13).
  RoundTrips(NpcShotAt{.player = 2,
                       .npc = 7,
                       .timeUs = 9,
                       .weapon = 0x0abcdef012ULL,
                       .trigger = 4,
                       .flags = kShotAtSmart | kShotAtTracked});
  RoundTrips(ShotHit{.player = 2, .npc = 7, .position = {1, 2, 3}, .direction = {0, -1, 0}});
  CHECK(std::string(NameOf(NpcShotAt{})) == "NpcShotAt");
  CHECK(std::string(NameOf(ShotHit{})) == "ShotHit");
  // Shots change state (a hit, a dodge): never dropped, never doubled.
  CHECK(ChannelOf(NpcShotAt{}) == Channel::Event);
  CHECK(IsReliable(ShotHit{}));
  RoundTrips(Install{.build = "0.2.0+2fa03ca",
                     .game = "3.0.88.46235",
                     .scripts = 0x1122334455667788,
                     .tweaks = 2,
                     .mods = 3,
                     .modCount = 41});
  CHECK(std::string(NameOf(Install{})) == "Install");
  CHECK(IsReliable(Install{}));
  CHECK_FALSE(Decode(Encode(Install{.build = std::string(kMaxInstallText + 1, 'x')})).has_value());
  CHECK(Encode(NpcShotAt{}).size() < 64);
  // Other mods (mod-compat.md): the lists, their names on request, the
  // substitutes told back; the hints next to records.
  RoundTrips(ModList{.kind = kModListed, .mods = {{.hash = 1, .categories = 2}, {.hash = 3}}});
  RoundTrips(ModList{.kind = kModNames, .mods = {{.hash = 1, .categories = 2, .name = "Hair2K"}}});
  RoundTrips(Substitutes{.owner = 2, .seer = 3, .kinds = 0x9, .count = 4});
  RoundTrips(Held{.player = 2,
                  .revision = 3,
                  .right = 0x1122334455,
                  .rightHint = {.type = 7, .tag = 8},
                  .leftHint = {.size = 2}});
  RoundTrips(
      Seat{.player = 2, .vehicle = 5, .seat = 6, .owner = 2, .hint = {.type = 9, .size = 4}});
  RoundTrips(Appearance{.player = 2, .items = {1, 2}, .hints = {{.type = 3}}});
  RoundTrips(NpcSpawn{.npc = 7, .record = 8, .hint = {.type = 1, .tag = 2}});
  CHECK(std::string(NameOf(ModList{})) == "ModList");
  CHECK(std::string(NameOf(Substitutes{})) == "Substitutes");
  CHECK(IsReliable(ModList{}));
  CHECK(IsReliable(Substitutes{}));
  CHECK_FALSE(Decode(Encode(ModList{.kind = 3})).has_value());
  CHECK_FALSE(
      Decode(Encode(ModList{.mods = {{.name = std::string(kMaxModName + 1, 'x')}}})).has_value());
  CHECK_FALSE(
      Decode(Encode(ModList{.mods = std::vector<ModEntry>(kMaxModEntries + 1)})).has_value());
  // Netcode (netcode.md): the compact pose and NPC streams.
  // Between keys a frame's time is its low 32 bits (PoseDecoder completes it).
  PoseFrame frame{.player = 2, .seq = 40, .timeUs = 1'800'000'000, .key = 31};
  frame.hips = {1, -2, 3};
  // Bone 0 whole, bone 2 turned, bone 3 turned in two bytes.
  frame.codes[0] = kBoneWhole | kBoneTurn << 4 | kBoneNudge << 6;
  frame.rotations.assign(kPoseFrameRotationBytes + kPoseTurnBytes + kPoseNudgeBytes, 7);
  RoundTrips(frame);
  CHECK(frame.Code(0) == kBoneWhole);
  CHECK(frame.Code(1) == kBoneKept);
  CHECK(frame.Code(2) == kBoneTurn);
  CHECK(frame.Code(3) == kBoneNudge);
  // 1 id, 1 player, 4 seq, 1 key distance, 4 time, the codes, 6 hips.
  CHECK(Encode(frame).size() == 17 + kPoseCodeBytes + frame.rotations.size());
  // A time that is not short does not go between keys.
  auto late = frame;
  late.timeUs = 1'800'000'000'000'000;
  const auto cut = Decode(Encode(late));
  REQUIRE(cut);
  CHECK(std::get<PoseFrame>(*cut).timeUs == (late.timeUs & kPoseTimeMask));
  // A key: its time whole, every bone whole, no codes on the wire.
  PoseFrame keyFrame{.player = 2, .seq = 41, .timeUs = 1'800'000'000'000'000, .key = 41};
  keyFrame.codes.fill(kBoneWhole | kBoneWhole << 2 | kBoneWhole << 4 | kBoneWhole << 6);
  keyFrame.rotations.assign(kPoseBones * kPoseFrameRotationBytes, 9);
  RoundTrips(keyFrame);
  CHECK(Encode(keyFrame).size() == 21 + keyFrame.rotations.size());
  // A key far back (a slow stream: an actor's) is still named.
  auto far = frame;
  far.key = far.seq - 70'000;
  RoundTrips(far);
  CHECK(std::string(NameOf(PoseFrame{})) == "PoseFrame");
  CHECK(ChannelOf(PoseFrame{}) == Channel::State);
  // The rotations must be exactly what the codes say (a whole bone, the last
  // bone's turn and a two-byte turn with no bytes for them), a key whole.
  auto lying = frame;
  lying.codes[0] |= kBoneWhole << 2;
  CHECK_FALSE(Decode(Encode(lying)));
  lying = frame;
  lying.codes[kPoseCodeBytes - 1] = 0x40;
  CHECK_FALSE(Decode(Encode(lying)));
  lying = frame;
  lying.codes[1] = 3;
  CHECK_FALSE(Decode(Encode(lying)));
  lying = frame;
  lying.key = lying.seq;
  CHECK_FALSE(Decode(Encode(lying)));
  RoundTrips(PlayerMotion{.player = 2,
                          .seq = 9,
                          .timeUs = 1'800'000'000'000'000,
                          .position = {-900.5f, 1880.25f, 36.f},
                          .yaw = 40000,
                          .velocity = {150, -20, 0},
                          .stance = 3,
                          .flags = 0x8001});
  // With the state before it (v22): 14 bytes more; without, 2.
  const PlayerMotion withBefore{
      .player = 2,
      .seq = 9,
      .timeUs = 1'800'000'000'000'000,
      .before = {.timeBack = 333, .offset = {-50, 3, 0}, .yaw = 39000, .velocity = {140, -20, 0}}};
  RoundTrips(withBefore);
  CHECK(Encode(PlayerMotion{}).size() == 39); // the player in a byte
  CHECK(Encode(withBefore).size() == 39 + 14);
  CHECK(Encode(PlayerMotion{}).size() < Encode(PlayerState{}).size() * 2 / 3 + 1);
  RoundTrips(VehicleMotion{.player = 2,
                           .seq = 9,
                           .timeUs = 1'800'000'000'000'000,
                           .position = {1, 2, 3},
                           .orientation = {1, 2, 3, 4, 5},
                           .velocity = {2000, 0, -5}});
  RoundTrips(VehicleMotion{.player = 2,
                           .seq = 9,
                           .timeUs = 1'800'000'000'000'000,
                           .before = {.timeBack = 333,
                                      .offset = {-600, 0, 2},
                                      .orientation = {5, 4, 3, 2, 1},
                                      .velocity = {1990, 0, -5}}});
  CHECK(Encode(VehicleMotion{}).size() == 39);
  CHECK(Encode(VehicleMotion{.before = {.timeBack = 1}}).size() == 39 + 17);
  CHECK(ChannelOf(PlayerMotion{}) == Channel::State);
  CHECK(ChannelOf(VehicleMotion{}) == Channel::State);
  RoundTrips(NpcMotion{.seq = 5,
                       .timeUs = 1'800'000'000'000'000,
                       .origin = {-900, 1880, 36},
                       .npcs = {{.npc = 7, .offset = {128, -256, 3}, .yaw = 16384, .detected = 2},
                                {.npc = 9, .velocity = {150, -20, 0}}}});
  CHECK(std::string(NameOf(NpcMotion{})) == "NpcMotion");
  CHECK(ChannelOf(NpcMotion{}) == Channel::State);
  CHECK(Encode(NpcMotion{.npcs = {NpcMove{}}}).size() - Encode(NpcMotion{}).size() == 19);
  CHECK(std::string(NameOfKind(9999)) == "?");
  RoundTrips(TimeEffect{.player = 3, .abilities = kTimeScanning | kTimeSlowMo, .scale = 1.f});
  CHECK(std::string(NameOf(TimeEffect{})) == "TimeEffect");
  RoundTrips(Transit{.player = 2,
                     .revision = 4,
                     .kind = TransitKind::Metro,
                     .flags = kTransitSkipping,
                     .at = 5,
                     .next = 6,
                     .vehicle = 0x1234'5678'9abcULL,
                     .seat = 0xb000b1d029d0cea0ULL});
  RoundTrips(
      Transit{.player = 1, .revision = 1, .kind = TransitKind::Scene, .flags = kTransitSeats});
  RoundTrips(Transit{.player = 3, .revision = 9, .kind = TransitKind::None});
  RoundTrips(Transit{
      .player = 2, .revision = 3, .kind = TransitKind::Along, .vehicle = 0x99, .carrier = 1});
  CHECK(std::string_view(NameOf(Transit{})) == "Transit");
  RoundTrips(NpcSeat{.npc = 7, .owner = 1, .seat = 0xb000b1d029d0cea0ULL});
  RoundTrips(NpcSeat{.npc = 7});
  CHECK(std::string_view(NameOf(NpcSeat{})) == "NpcSeat");
  // World vehicles and police.
  RoundTrips(TrafficSpawn{.id = MakeTrafficId(1, 9),
                          .record = 0x1122334455,
                          .appearance = 0x99,
                          .entity = 0xABCDEF,
                          .position = {1, 2, 3},
                          .orientation = {0, 0, 0.6f, 0.8f},
                          .flags = kTrafficPolice | kTrafficSiren});
  RoundTrips(TrafficStates{
      .seq = 4,
      .timeUs = 5'000'000,
      .cars = {
          {.id = 9, .position = {1, 2, 3}, .orientation = {1, -2, 3}, .velocity = {-4, 5, 6}}}});
  RoundTrips(TrafficGone{.id = 9, .reason = kTrafficTaken, .player = 2});
  RoundTrips(TrafficStatus{.id = 9, .health = 0.25f, .flags = kTrafficBurning});
  RoundTrips(TrafficHit{.player = 2,
                        .id = 9,
                        .damage = 120.f,
                        .flags = kTrafficHitCollision,
                        .position = {1, 2, 3},
                        .impulse = {4, 5, 6}});
  RoundTrips(TrafficClaim{.player = 2, .id = 9, .seat = 0xb000b1d029d0cea0ULL});
  RoundTrips(TrafficSet{.ids = {1, 2, 3}});
  RoundTrips(Heat{.player = 2, .revision = 3, .stage = 4, .state = HeatState::Searching});
  RoundTrips(Crime{.player = 2,
                   .kind = CrimeKind::Damage,
                   .flags = kCrimePolice,
                   .attackType = 10,
                   .amount = 0.3f,
                   .npc = 7,
                   .traffic = 9,
                   .position = {1, 2, 3}});
  CHECK(std::string_view(NameOf(TrafficSpawn{})) == "TrafficSpawn");
  CHECK(std::string_view(NameOf(TrafficSet{})) == "TrafficSet");
  CHECK(std::string_view(NameOf(Heat{})) == "Heat");
  CHECK(std::string_view(NameOf(Crime{.kind = CrimeKind::Witness})) == "Crime");
  RoundTrips(VehicleDamage{.vehicle = StreetCar(MakeTrafficId(1, 3)),
                           .revision = 7,
                           .record = 0x1122334455ULL,
                           .health = 0.25f,
                           .flags = kDamageBurning,
                           .tyres = 0b1010,
                           .windows = 0b11,
                           .grid = {1, 2, 3, 255},
                           .parts = {0xabcdefULL, 0x123456ULL},
                           .explodedUs = 123'456'789'012ULL,
                           .instigator = 2});
  RoundTrips(VehicleHit{.player = 2,
                        .vehicle = PlayerCar(3),
                        .damage = 40.f,
                        .flags = kVehicleHitTyre,
                        .tyre = 3,
                        .position = {1, 2, 3},
                        .direction = {0, 1, 0}});
  RoundTrips(VehicleExit{.player = 2,
                         .vehicle = PlayerCar(2),
                         .kind = ExitKind::Knocked,
                         .flags = kExitRolling | kExitDriver,
                         .velocity = {9, 1, 0}});
  RoundTrips(VehicleCombat{
      .player = 2, .revision = 4, .mode = SeatCombat::Gunner, .vehicle = 1, .weapon = 0x99});
  RoundTrips(MountedFire{
      .player = 2, .seq = 5, .vehicle = 1, .weapon = 0x10, .slot = 0x20, .target = {4, 5, 6}});
  RoundTrips(GunnerRequest{.player = 2, .vehicle = 1, .seat = 0xfeedULL, .leave = true});
  RoundTrips(Gunner{.vehicle = 1, .gunner = 2, .seat = 0xfeedULL, .revision = 3});
  RoundTrips(GunnerAim{.player = 2,
                       .seq = 8,
                       .timeUs = 99'000'000ULL,
                       .vehicle = 1,
                       .yaw = 0.5f,
                       .pitch = -0.1f,
                       .flags = kAimFiring});
  RoundTrips(VehicleBump{
      .player = 2, .other = 3, .impulse = {0, 4, 0}, .position = {1, 1, 1}, .atUs = 42});
  RoundTrips(NpcRam{.player = 2,
                    .npc = 7,
                    .velocity = {20, 0, 0},
                    .position = {1, 2, 3},
                    .flags = kRamKnockdown | kRamLethal});
  RoundTrips(Chase{.vehicle = MakeTrafficId(1, 4), .target = 2, .flags = kChaseShooting});
  CHECK(std::string_view(NameOf(VehicleDamage{})) == "VehicleDamage");
  CHECK(std::string_view(NameOf(Chase{})) == "Chase");
  CHECK(IsVehicleCombat(VehicleDamage{}));
  CHECK(IsVehicleCombat(Chase{}));
  CHECK_FALSE(IsVehicleCombat(Crime{.kind = CrimeKind::Witness}));
  CHECK(ChannelOf(GunnerAim{}) == Channel::State);
  CHECK(ChannelOf(MountedFire{}) == Channel::State);
  CHECK(ChannelOf(VehicleDamage{}) == Channel::Event);
  // One name covers both kinds of vehicle, and says whose it is.
  CHECK(OwnerOfVehicle(PlayerCar(3)) == 3);
  CHECK(OwnerOfVehicle(StreetCar(MakeTrafficId(2, 5))) == 2);
  CHECK(IsStreetCar(StreetCar(MakeTrafficId(1, 1))));
  CHECK_FALSE(IsStreetCar(PlayerCar(1)));
  {
    VehicleDamage many;
    many.parts.resize(kMaxDetachedParts + 1);
    CHECK_FALSE(Decode(Encode(many)).has_value());
    CHECK_FALSE(Decode(Encode(VehicleExit{.kind = static_cast<ExitKind>(9)})).has_value());
    CHECK_FALSE(Decode(Encode(VehicleCombat{.mode = static_cast<SeatCombat>(9)})).has_value());
  }
  RoundTrips(NpcHack{
      .player = 2, .npc = 7, .action = 0x1122334455, .uploadMs = 1500, .flags = kUploadTraceable});
  RoundTrips(NpcHackState{
      .npc = 7, .player = 2, .action = 0x1122334455, .step = HackStep::Done, .uploadMs = 1500});
  RoundTrips(NpcStatus{.npc = 7, .player = 1, .effect = 0xAB, .applied = true});
  RoundTrips(NpcStatus{.npc = 7, .player = 0, .effect = 0xAB, .applied = false});
  RoundTrips(PlayerHack{
      .player = 2, .npc = 7, .action = 0x99, .flags = kPlayerHackReveal | kPlayerHackDirectional});
  RoundTrips(VehicleHack{.player = 2, .driver = 1, .hack = CarHack::Accelerate, .seconds = 4.5f});
  RoundTrips(VehicleRadio{.player = 2,
                          .revision = 5,
                          .on = true,
                          .station = 13,
                          .track = 0xfeedfacecafef00dULL,
                          .trackStartUs = 123'456'789'012ULL});
  RoundTrips(VehicleRadio{.player = 3, .revision = 1, .on = false, .station = -1});
  RoundTrips(PlayerHit{.player = 2,
                       .npc = 7,
                       .physical = 29.5f,
                       .thermal = 3.f,
                       .electric = 0.5f,
                       .chemical = 1.f,
                       .attackType = 10,
                       .flags = kHitWeakSpot | kHitCritical,
                       .pierce = 0.25f,
                       .position = {1, 2, 3},
                       .direction = {0, -1, 0},
                       .attack = 0x1234567890ULL,
                       .weapon = 0x0abcdef012ULL});
  RoundTrips(Regroup{.from = 2,
                     .to = 0,
                     .offer = 4,
                     .reason = RegroupReason::FastTravel,
                     .point = 0x1b2c3d4e5fULL,
                     .marker = 0xcbf29ce484222325ULL,
                     .position = {-915.f, 1879.5f, 36.3f},
                     .yaw = 1.5f});
  RoundTrips(Regroup{
      .from = 1, .to = 3, .offer = 1, .reason = RegroupReason::Story, .label = "The Heist"});
  RoundTrips(RegroupAnswer{.from = 3, .to = 1, .offer = 1, .accepted = true});
  RoundTrips(RegroupAnswer{.from = 2, .to = 1, .offer = 0, .accepted = false});
  RoundTrips(WorldItem{.id = 4,
                       .dropper = 2,
                       .token = 9,
                       .position = {1.f, 2.f, 3.f},
                       .stacks = {{.record = 0x1122334455ULL, .seed = 77, .quantity = 1},
                                  {.record = 0x66ULL, .seed = 0, .quantity = 0}}});
  RoundTrips(WorldItemTaken{.id = 4, .stack = 1, .taker = 3});
  RoundTrips(SharedItem{.player = 2,
                        .kind = kSharedOnCorpse,
                        .npc = 7,
                        .item = {.record = 0x0a0b0c0d0eULL, .seed = 5, .quantity = 1}});
  RoundTrips(NpcAnims{.npcs = {{.npc = 7,
                                .highLevel = 2,
                                .upperBody = 0,
                                .stance = 2,
                                .flags = kNpcAnimWeaponDrawn,
                                .weapon = 0x1122334455667788ULL,
                                .target = 2},
                               {.npc = 8,
                                .flags = kNpcAnimInWorkspot,
                                .workspot = 0xabcdef0123456789ULL,
                                .workspotEntry = 3,
                                .workspotAnim = 0x99}}});
  RoundTrips(NpcShot{.npc = 7, .effect = 16, .target = {1, 2, 3}});
}

TEST_CASE("a regroup offer is a reliable event with a bounded label") {
  CHECK(ChannelOf(Regroup{}) == Channel::Event);
  CHECK(IsReliable(RegroupAnswer{}));
  CHECK(std::string_view(NameOf(Regroup{})) == "Regroup");
  CHECK(std::string_view(NameOf(RegroupAnswer{})) == "RegroupAnswer");
  Regroup offer{.label = std::string(kMaxRegroupLabel, 'x')};
  CHECK(Decode(Encode(offer)).has_value());
  CHECK(Encode(offer).size() < 160);
  offer.label.push_back('x');
  CHECK_FALSE(Decode(Encode(offer)).has_value());
}

TEST_CASE("a link request not taken is a reliable event") {
  RoundTrips(LinkDecline{.from = 3, .to = 2, .reason = kLinkDeclineOther});
  RoundTrips(LinkDecline{.from = 3, .to = 2, .reason = kLinkDeclineMissed});
  CHECK(ChannelOf(LinkDecline{}) == Channel::Event);
  CHECK(std::string_view(NameOf(LinkDecline{})) == "LinkDecline");
}

TEST_CASE("world items are reliable events, and a bag has a bound") {
  CHECK(ChannelOf(WorldItem{}) == Channel::Event);
  CHECK(IsReliable(WorldItemTaken{}));
  CHECK(std::string_view(NameOf(WorldItem{})) == "WorldItem");
  CHECK(std::string_view(NameOf(WorldItemTaken{})) == "WorldItemTaken");
  CHECK(std::string_view(NameOf(SharedItem{})) == "SharedItem");
  CHECK(IsReliable(SharedItem{}));
  CHECK(Encode(SharedItem{}).size() < 64);
  WorldItem bag{.id = 1};
  bag.stacks.resize(kMaxWorldItemStacks);
  CHECK(Encode(bag).size() < 1400); // one datagram
  CHECK(Decode(Encode(bag)).has_value());
  bag.stacks.resize(kMaxWorldItemStacks + 1);
  CHECK_FALSE(Decode(Encode(bag)).has_value());
}

TEST_CASE("NPC animation states are reliable and small, their shots unreliable") {
  CHECK(ChannelOf(NpcAnims{}) == Channel::Event);
  CHECK(IsReliable(NpcAnims{}));
  CHECK(ChannelOf(NpcShot{}) == Channel::State);
  CHECK(std::string_view(NameOf(NpcAnims{})) == "NpcAnims");
  CHECK(std::string_view(NameOf(NpcShot{})) == "NpcShot");
  CHECK(Encode(NpcShot{}).size() < 32);
  NpcAnims crowd;
  crowd.npcs.resize(kMaxNpcAnims);
  CHECK(Encode(crowd).size() < 4096); // all 64 at once, rare (a fight starting)
  CHECK(Encode(NpcAnims{.npcs = {NpcAnim{}}}).size() < 64);
  crowd.npcs.resize(kMaxNpcAnims + 1);
  CHECK_FALSE(Decode(Encode(crowd)).has_value());
}

TEST_CASE("hit outcomes survive encoding, travel reliably and are refused out of range") {
  RoundTrips(NpcStrike{.player = 2,
                       .npc = 7,
                       .damage = 41.5f,
                       .poise = 12.f,
                       .minHealth = 10.f,
                       .flags = kStrikeWeakSpot | kStrikeNonlethal | kStrikeForceDismember,
                       .attackType = 7,
                       .zone = 0,
                       .part = 4,
                       .shape = 0,
                       .attack = 0x1234567890ULL,
                       .position = {1, 2, 3},
                       .direction = {0, 1, 0}});
  RoundTrips(NpcWound{.npc = 7, .player = 2, .part = 1, .wound = 0, .position = {1, 2, 3}});
  RoundTrips(NpcPoise{.npc = 7, .poise = 0.25f});
  RoundTrips(Difficulty{.level = 2});
  RoundTrips(AreaHit{.from = 3,
                     .player = 2,
                     .physical = 30.f,
                     .thermal = 5.f,
                     .attackType = 3,
                     .flags = kHitCritical,
                     .position = {1, 2, 3},
                     .direction = {0, 0, -1},
                     .attack = 0x99,
                     .weapon = 0x42});
  for (const Message message : {Message(NpcStrike{}), Message(NpcWound{}), Message(NpcPoise{}),
                                Message(Difficulty{}), Message(AreaHit{})}) {
    CHECK(IsCombat(message));
    CHECK_FALSE(IsStory(message));
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK(Encode(message).size() < 80);
  }
  CHECK_FALSE(IsCombat(NpcHit{}));
  CHECK_FALSE(IsCombat(QuestReward{}));
  CHECK(std::string_view(NameOf(NpcStrike{})) == "NpcStrike");
  CHECK(std::string_view(NameOf(AreaHit{})) == "AreaHit");
  CHECK_FALSE(Decode(Encode(NpcWound{.part = 6})).has_value());
  CHECK_FALSE(Decode(Encode(NpcWound{.wound = 3})).has_value());
  CHECK_FALSE(Decode(Encode(Difficulty{.level = 4})).has_value());
  CHECK(Decode(Encode(Difficulty{.level = 3})).has_value());
}

TEST_CASE("a car's radio is a small reliable event") {
  CHECK(ChannelOf(VehicleRadio{}) == Channel::Event);
  CHECK(IsReliable(VehicleRadio{}));
  CHECK(std::string_view(NameOf(VehicleRadio{})) == "VehicleRadio");
  CHECK(Encode(VehicleRadio{}).size() < 64);
  RoundTrips(NpcAwareness{
      .seq = 3,
      .timeUs = 7'000'000,
      .entries = {{.npc = 7, .player = 2, .level = 128}, {.npc = 9, .player = 3, .level = 255}}});
  RoundTrips(Stealth{.player = 2,
                     .revision = 4,
                     .flags = kStealthCamo | kStealthCrouch,
                     .visibility = 0.35f,
                     .camo = 0x1a2b3c4d5eULL});
  RoundTrips(Noise{.player = 2, .stim = 23, .radius = 25.f, .propagation = true});
  CHECK(std::string(NameOf(Noise{})) == "Noise");
  CHECK(std::string(NameOf(NpcAwareness{})) == "NpcAwareness");
  CHECK(std::string(NameOf(Stealth{})) == "Stealth");
}

TEST_CASE("stealth travels reliably, awareness and noise as state") {
  CHECK(ChannelOf(NpcAwareness{}) == Channel::State);
  CHECK(ChannelOf(Noise{}) == Channel::State);
  CHECK(ChannelOf(Stealth{}) == Channel::Event);
  CHECK(IsReliable(Stealth{}));
  NpcAwareness crowd;
  crowd.entries.resize(kMaxNpcAwareness);
  CHECK(Encode(crowd).size() < 1400); // one datagram
  crowd.entries.resize(kMaxNpcAwareness + 1);
  CHECK_FALSE(Decode(Encode(crowd)).has_value());
  CHECK(Encode(Noise{}).size() < 32);
}

TEST_CASE("takedown messages survive encoding") {
  RoundTrips(NpcAction{.player = 2,
                       .npc = 7,
                       .kind = NpcActionKind::Finisher,
                       .name = 0xfeedULL,
                       .item = 0x2a1bULL,
                       .amount = 0.2f,
                       .direction = {0, 100, 10}});
  RoundTrips(NpcAction{.npc = 8, .kind = NpcActionKind::Dispose, .item = 0x123456789ULL});
  RoundTrips(NpcActionReply{.npc = 7, .kind = NpcActionKind::Grab, .accepted = true});
  RoundTrips(NpcActionReply{
      .npc = 7, .kind = NpcActionKind::PickUp, .accepted = false, .reason = kRefusedHeld});
  RoundTrips(NpcHold{.npc = 7, .player = 2, .how = NpcHoldKind::Grappled, .name = 0xbeef});
  RoundTrips(NpcHold{.npc = 7});
  RoundTrips(NpcCue{.npc = 7, .player = 3, .cue = NpcCueKind::PoiseBroken});
  RoundTrips(NpcCue{.npc = 7, .player = 2, .cue = NpcCueKind::OutOfSight});
  RoundTrips(NpcGone{.npc = 7, .reason = kGoneDisposed, .killer = 2});
}

TEST_CASE("NPC actions travel reliably and unknown kinds are refused") {
  CHECK(ChannelOf(NpcAction{}) == Channel::Event);
  CHECK(ChannelOf(NpcActionReply{}) == Channel::Event);
  CHECK(ChannelOf(NpcHold{}) == Channel::Event);
  CHECK(ChannelOf(NpcCue{}) == Channel::Event);
  CHECK(IsReliable(NpcAction{.kind = NpcActionKind::Grab}));
  CHECK(std::string(NameOf(NpcAction{})) == "NpcAction");
  CHECK(std::string(NameOf(NpcActionReply{})) == "NpcActionReply");
  CHECK(std::string(NameOf(NpcHold{})) == "NpcHold");
  CHECK(std::string(NameOf(NpcCue{})) == "NpcCue");
  CHECK(std::string(NameOf(Pause{})) == "Pause");
  CHECK_FALSE(Decode(Encode(NpcAction{.kind = NpcActionKind{0}})).has_value());
  CHECK_FALSE(Decode(Encode(NpcAction{.kind = NpcActionKind{11}})).has_value());
  CHECK(Decode(Encode(NpcAction{.kind = NpcActionKind::Dispose})).has_value());
  CHECK_FALSE(Decode(Encode(NpcActionReply{.kind = NpcActionKind{0}})).has_value());
  CHECK_FALSE(Decode(Encode(NpcHold{.how = NpcHoldKind{5}})).has_value());
  CHECK_FALSE(Decode(Encode(NpcCue{.cue = NpcCueKind{0}})).has_value());
  CHECK(Decode(Encode(NpcCue{.cue = NpcCueKind::OutOfSight})).has_value());
  CHECK_FALSE(Decode(Encode(NpcCue{.cue = NpcCueKind{4}})).has_value());
  // Small: a grapple is one request, not a stream.
  CHECK(Encode(NpcAction{}).size() < 64);
}

TEST_CASE("an appearance past its limits is refused") {
  Appearance big;
  big.look.resize(kMaxAppearanceKeys + 1);
  CHECK_FALSE(Decode(Encode(big)).has_value());
  big.look.resize(kMaxAppearanceKeys);
  CHECK(Decode(Encode(big)).has_value());
  CHECK(IsReliable(Appearance{}));
  CHECK(IsReliable(Held{}));
  CHECK(ChannelOf(Shot{}) == Channel::State);
  CHECK(ChannelOf(VehicleState{}) == Channel::State);
  CHECK(IsReliable(Seat{}));
  CHECK(IsReliable(World{}));
  CHECK(IsReliable(NpcSpawn{}));
  CHECK(ChannelOf(NpcStates{}) == Channel::State);
  CHECK(ChannelOf(NpcHit{}) == Channel::Event);
  CHECK(ChannelOf(NpcHealth{}) == Channel::Event);
  CHECK(ChannelOf(NpcEffect{}) == Channel::Event);
  CHECK(ChannelOf(Pause{}) == Channel::Event);
  CHECK(IsReliable(Pause{}));
  CHECK(ChannelOf(TimeEffect{}) == Channel::Event);
  CHECK(IsReliable(TimeEffect{}));
  // Rides and seats change state: never dropped, and not story messages.
  CHECK(ChannelOf(Transit{}) == Channel::Event);
  CHECK(IsReliable(Transit{}));
  CHECK_FALSE(IsStory(Transit{}));
  CHECK(ChannelOf(NpcSeat{}) == Channel::Event);
  CHECK(IsReliable(NpcSeat{}));
  CHECK_FALSE(IsStory(NpcSeat{}));
  // World vehicles: transforms are state (newest wins), the rest changes
  // what every game shows and is never dropped.
  CHECK(ChannelOf(TrafficStates{}) == Channel::State);
  CHECK_FALSE(IsReliable(TrafficStates{}));
  CHECK(ChannelOf(TrafficSpawn{}) == Channel::Event);
  CHECK(ChannelOf(TrafficGone{}) == Channel::Event);
  CHECK(ChannelOf(TrafficStatus{}) == Channel::Event);
  CHECK(ChannelOf(TrafficHit{}) == Channel::Event);
  CHECK(ChannelOf(TrafficClaim{}) == Channel::Event);
  CHECK(ChannelOf(TrafficSet{}) == Channel::Event);
  CHECK(ChannelOf(Heat{}) == Channel::Event);
  CHECK(ChannelOf(Crime{}) == Channel::Event);
  CHECK_FALSE(IsStory(Heat{}));
  CHECK_FALSE(IsCombat(Crime{}));
  // A full packet of moving vehicles stays small: 32 of them under 1 kB.
  {
    TrafficStates full;
    full.cars.resize(kMaxTrafficPerState);
    CHECK(Encode(full).size() <= 28 * kMaxTrafficPerState + 24);
    full.cars.resize(kMaxTrafficPerState + 1);
    CHECK_FALSE(Decode(Encode(full)).has_value());
    TrafficSet set;
    set.ids.resize(kMaxTrafficSet + 1);
    CHECK_FALSE(Decode(Encode(set)).has_value());
  }
  // Heat beyond five stars, a state or a crime we do not know: refused.
  CHECK(Decode(Encode(Heat{.stage = kMaxHeat, .state = HeatState::Blinking})).has_value());
  CHECK_FALSE(Decode(Encode(Heat{.stage = kMaxHeat + 1})).has_value());
  CHECK_FALSE(Decode(Encode(Heat{.state = static_cast<HeatState>(4)})).has_value());
  CHECK_FALSE(Decode(Encode(Crime{})).has_value());
  CHECK_FALSE(Decode(Encode(Crime{.kind = static_cast<CrimeKind>(5)})).has_value());
  CHECK_FALSE(Decode(Encode(TrafficGone{.reason = 3})).has_value());
  // Ids: the owner in the top byte, and a seat in a shared vehicle is told
  // from a seat in a player's car.
  CHECK(TrafficOwner(MakeTrafficId(3, 77)) == 3);
  CHECK(MakeTrafficId(1, 0x1FFFFFF) == MakeTrafficId(1, 0xFFFFFF));
  CHECK(IsTrafficSeat(TrafficSeatOwner(MakeTrafficId(1, 5))));
  CHECK(TrafficOfSeat(TrafficSeatOwner(MakeTrafficId(1, 5))) == MakeTrafficId(1, 5));
  CHECK_FALSE(IsTrafficSeat(PlayerId{4}));
  // A ride of a kind we do not know is refused, not guessed at.
  CHECK(Decode(Encode(Transit{.kind = TransitKind::Along})).has_value());
  Transit unknown{.kind = static_cast<TransitKind>(9)};
  CHECK_FALSE(Decode(Encode(unknown)).has_value());
  // Quickhacks change state: never dropped.
  CHECK(ChannelOf(NpcHack{}) == Channel::Event);
  CHECK(ChannelOf(NpcHackState{}) == Channel::Event);
  CHECK(ChannelOf(NpcStatus{}) == Channel::Event);
  CHECK(ChannelOf(PlayerHack{}) == Channel::Event);
  CHECK(ChannelOf(VehicleHack{}) == Channel::Event);
  CHECK(IsReliable(VehicleHack{}));
  CHECK(std::string_view(NameOf(VehicleHack{})) == "VehicleHack");
  CHECK(std::string_view(NameOf(NpcHack{})) == "NpcHack");
  CHECK(ChannelOf(PlayerHit{}) == Channel::Event);
  CHECK(IsReliable(PlayerHit{}));
  CHECK(std::string_view(NameOf(PlayerHit{})) == "PlayerHit");
  // A guest cannot flood the host with effects.
  NpcEffect many{.npc = 1, .effects = std::vector<std::uint64_t>(kMaxNpcEffects + 1, 5)};
  CHECK_FALSE(Decode(Encode(many)));
  NpcStates crowd;
  // (An owner cuts its list to a datagram a message: netcode_test.)
  crowd.npcs.resize(kMaxNpcsPerState);
  CHECK(Decode(Encode(crowd)).has_value());
  crowd.npcs.resize(kMaxNpcsPerState + 1);
  CHECK_FALSE(Decode(Encode(crowd)).has_value());
  CHECK(Encode(VehicleState{}).size() < 64);
}

TEST_CASE("quickhack messages out of range are refused") {
  auto step = Encode(NpcHackState{.step = HackStep::Failed});
  CHECK(Decode(step).has_value());
  auto car = Encode(VehicleHack{.hack = CarHack::Accelerate});
  CHECK(Decode(car).has_value());
  NpcHackState badStep{.step = static_cast<HackStep>(3)};
  CHECK_FALSE(Decode(Encode(badStep)).has_value());
  VehicleHack badCar{.hack = static_cast<CarHack>(7)};
  CHECK_FALSE(Decode(Encode(badCar)).has_value());
}

TEST_CASE("broken input is refused, never half-read") {
  auto bytes = Encode(Ping{1});
  bytes.push_back(std::byte{0});
  CHECK_FALSE(Decode(bytes).has_value());

  const std::byte unknownIndex[] = {std::byte{200}};
  CHECK_FALSE(Decode(unknownIndex).has_value());

  auto truncated = Encode(Hello{.name = "a long enough name"});
  truncated.resize(truncated.size() / 2);
  CHECK_FALSE(Decode(truncated).has_value());

  CHECK_FALSE(Decode({}).has_value());
}

TEST_CASE("player state is unreliable, everything else reliable") {
  CHECK(ChannelOf(PlayerState{}) == Channel::State);
  CHECK_FALSE(IsReliable(PlayerState{}));
  CHECK(IsReliable(Hello{}));
  CHECK(IsReliable(Roster{}));
}

TEST_CASE("a player state stays small") {
  // Sent 30 times a second per player: keep it well under one MTU.
  CHECK(Encode(PlayerState{}).size() < 64);
}

TEST_CASE("story messages survive encoding") {
  RoundTrips(Journal{.snapshot = true, .entries = {{0x1234, 2, 0}, {0xFFFFFFFF, 3, 1}}});
  RoundTrips(Journal{.snapshot = false, .entries = {{7, 4, 1}}});
  RoundTrips(Tracked{.hash = 0xCAFE});
  RoundTrips(PhoneCall{.contact = 0x1122334455667788,
                       .phase = 1,
                       .mode = 1,
                       .visuals = 0,
                       .flags = kCallRejectable | kCallShowAvatar,
                       .answeredBy = 2});
  RoundTrips(PhoneAction{.player = 2, .action = kPhoneAnswer, .contact = 99});
  RoundTrips(PhoneReply{.player = 3, .choiceEntry = 0xABCD});
  RoundTrips(Subtitle{.line = 0x0102030405060708,
                      .type = 2,
                      .flags = kLinePersistent,
                      .duration = 3.5f,
                      .speaker = 12,
                      .speakerPlayer = 1,
                      .speakerName = "Judy",
                      .text = "Hey, V."});
  RoundTrips(ChoiceHub{.hub = 77,
                       .flags = kHubPhoneLock,
                       .title = "Judy",
                       .options = {{"Sure.", 1 << 0}, {"[Lie] No.", 1 << 8}},
                       .timedSeconds = 5.f,
                       .timedStartUs = 123});
  RoundTrips(ChoiceHub{.hub = 77});
  RoundTrips(ChoiceCursor{.player = 2, .hub = 77, .index = -1, .kind = kCursorSuggest});
  RoundTrips(StoryState{.player = 1, .tier = 4, .flags = kStoryInCall});
  // Spectating: the host's moment, a guest watching it (free bits of flags).
  RoundTrips(StoryState{.player = 1, .tier = 2, .flags = kStoryMoment | kStoryReplacer});
  RoundTrips(StoryState{.player = 2, .tier = 1, .flags = kStorySpectating});
  RoundTrips(StoryState{.player = 1, .tier = 5, .flags = kStoryMoment | kStoryWatch});
  // The host's quest blocks fast travel: the top bit, with others.
  RoundTrips(StoryState{.player = 1, .tier = 1, .flags = kStoryNoFastTravel});
  RoundTrips(StoryState{.player = 1, .tier = 3, .flags = kStoryNoFastTravel | kStoryMoment});
  RoundTrips(QuestReward{.reward = 0x15e213846eULL, .quest = 0x77});
}

TEST_CASE("story messages: channels, limits, sizes") {
  CHECK(ChannelOf(Journal{.snapshot = true}) == Channel::Bulk);
  CHECK(ChannelOf(Journal{.snapshot = false}) == Channel::Event);
  CHECK(IsReliable(Journal{.snapshot = true}));
  for (const Message message :
       {Message{Tracked{}}, Message{PhoneCall{}}, Message{PhoneAction{}}, Message{PhoneReply{}},
        Message{Subtitle{}}, Message{ChoiceHub{}}, Message{ChoiceCursor{}}, Message{StoryState{}},
        Message{QuestReward{}}}) {
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK(IsStory(message));
  }
  CHECK(IsStory(Journal{}));
  CHECK_FALSE(IsStory(Pause{}));
  CHECK_FALSE(IsStory(Hello{}));
  CHECK(std::string(NameOf(QuestReward{})) == "QuestReward");
  CHECK(std::string(NameOf(Journal{})) == "Journal");

  Journal big{.snapshot = true};
  big.entries.resize(kMaxJournalEntries + 1);
  CHECK_FALSE(Decode(Encode(big)).has_value());
  big.entries.resize(kMaxJournalEntries);
  CHECK(Decode(Encode(big)).has_value());
  ChoiceHub wide{.hub = 1};
  wide.options.resize(kMaxChoiceOptions + 1);
  CHECK_FALSE(Decode(Encode(wide)).has_value());
  // A typical journal (a few thousand entries) is tens of KB, on Bulk.
  Journal typical{.snapshot = true};
  typical.entries.resize(3000);
  CHECK(Encode(typical).size() < 32 * 1024);
}

TEST_CASE("a story effect survives encoding and is a story message") {
  RoundTrips(StoryEffect{.player = 1, .effect = 0x15e213846eULL, .applied = true});
  CHECK(IsStory(StoryEffect{}));
  CHECK(ChannelOf(StoryEffect{}) == Channel::Event);
  CHECK(std::string(NameOf(StoryEffect{})) == "StoryEffect");
}

TEST_CASE("story restrictions survive encoding, are a story message, and say one set one way") {
  RoundTrips(StoryRestrictions{.player = 1, .effects = {0x10, 0x15e213846eULL}});
  RoundTrips(StoryRestrictions{.player = 1});
  CHECK(IsStory(StoryRestrictions{}));
  CHECK(ChannelOf(StoryRestrictions{}) == Channel::Event);
  CHECK(std::string(NameOf(StoryRestrictions{})) == "StoryRestrictions");
  // Unsorted, repeated or too many: refused.
  CHECK_FALSE(Decode(Encode(StoryRestrictions{.effects = {2, 1}})).has_value());
  CHECK_FALSE(Decode(Encode(StoryRestrictions{.effects = {1, 1}})).has_value());
  StoryRestrictions many;
  for (std::uint64_t i = 0; i <= kMaxStoryRestrictions; ++i)
    many.effects.push_back(i + 1);
  CHECK_FALSE(Decode(Encode(many)).has_value());
  many.effects.pop_back();
  CHECK(Decode(Encode(many)).has_value());
}

TEST_CASE("open-world activities done survive encoding and are a story message") {
  RoundTrips(OpenWorldDone{.player = 1, .markers = {0x10, 0x8f3a5c2eU}});
  RoundTrips(OpenWorldDone{.player = 1});
  CHECK(IsStory(OpenWorldDone{}));
  CHECK(ChannelOf(OpenWorldDone{}) == Channel::Event);
  CHECK(std::string(NameOf(OpenWorldDone{})) == "OpenWorldDone");
  // Unsorted, repeated, a marker 0 or too many: refused.
  CHECK_FALSE(Decode(Encode(OpenWorldDone{.markers = {2, 1}})).has_value());
  CHECK_FALSE(Decode(Encode(OpenWorldDone{.markers = {1, 1}})).has_value());
  CHECK_FALSE(Decode(Encode(OpenWorldDone{.markers = {0, 1}})).has_value());
  OpenWorldDone many;
  for (std::uint32_t i = 0; i <= kMaxOpenWorldDone; ++i)
    many.markers.push_back(i + 1);
  CHECK_FALSE(Decode(Encode(many)).has_value());
  many.markers.pop_back();
  CHECK(Decode(Encode(many)).has_value());
}

TEST_CASE("world prefabs and story gates survive encoding and are story messages") {
  RoundTrips(WorldPrefabs{.player = 1, .prefabs = {0x10, 0x8f3a5c2e11d04b77ULL}});
  RoundTrips(WorldPrefabs{.player = 1});
  CHECK(IsStory(WorldPrefabs{}));
  CHECK(ChannelOf(WorldPrefabs{}) == Channel::Event);
  CHECK(std::string(NameOf(WorldPrefabs{})) == "WorldPrefabs");
  // Unsorted, repeated, a node 0 or too many: refused.
  CHECK_FALSE(Decode(Encode(WorldPrefabs{.prefabs = {2, 1}})).has_value());
  CHECK_FALSE(Decode(Encode(WorldPrefabs{.prefabs = {1, 1}})).has_value());
  CHECK_FALSE(Decode(Encode(WorldPrefabs{.prefabs = {0, 1}})).has_value());
  WorldPrefabs many;
  for (std::uint64_t i = 0; i <= kMaxWorldPrefabs; ++i)
    many.prefabs.push_back(i + 1);
  CHECK_FALSE(Decode(Encode(many)).has_value());
  many.prefabs.pop_back();
  CHECK(Decode(Encode(many)).has_value());

  RoundTrips(StoryGates{.player = 2, .flags = kGateWatsonOpen | kGateInDogtown});
  CHECK(IsStory(StoryGates{}));
  CHECK(ChannelOf(StoryGates{}) == Channel::Event);
  CHECK(std::string(NameOf(StoryGates{})) == "StoryGates");
  CHECK_FALSE(Decode(Encode(StoryGates{.flags = 0x10})).has_value());
}

TEST_CASE("a quest interaction survives encoding, is a story message, and is small") {
  RoundTrips(QuestInteract{.target = 0x1122334455667788ULL,
                           .choice = 0xAABB,
                           .detail = 0xCCDD,
                           .layer = 0xEE,
                           .seq = 3,
                           .position = {1.f, 2.f, 3.f},
                           .timeUs = 99,
                           .player = 2,
                           .verdict = kInteractPerformed,
                           .flags = kInteractLoot});
  RoundTrips(QuestInteract{.target = 1});
  CHECK(IsStory(QuestInteract{}));
  CHECK(ChannelOf(QuestInteract{}) == Channel::Event);
  CHECK(std::string(NameOf(QuestInteract{})) == "QuestInteract");
  CHECK(Encode(QuestInteract{.target = 1}).size() <= 72);
  CHECK_FALSE(Decode(Encode(QuestInteract{.target = 1, .verdict = 8})).has_value());
  CHECK_FALSE(Decode(Encode(QuestInteract{.target = 1, .flags = 8})).has_value());
  CHECK_FALSE(Decode(Encode(QuestInteract{})).has_value()); // no object
}

TEST_CASE("status visuals survive encoding, are kept state, and say one set one way") {
  RoundTrips(StatusVisuals{.player = 2, .revision = 4, .records = {0x10, 0x15e213846eULL}});
  RoundTrips(StatusVisuals{.player = 1, .revision = 1});
  CHECK(ChannelOf(StatusVisuals{}) == Channel::Event);
  CHECK(IsReliable(StatusVisuals{}));
  CHECK_FALSE(IsStory(StatusVisuals{}));
  CHECK_FALSE(IsCombat(StatusVisuals{}));
  CHECK(std::string(NameOf(StatusVisuals{})) == "StatusVisuals");
  // Appended last, so every earlier message keeps its id (no version bump).
  CHECK(Message(StatusVisuals{}).index() == Message(NpcVendor{}).index() + 1);
  // Unsorted, repeated or too many: refused.
  CHECK_FALSE(Decode(Encode(StatusVisuals{.records = {2, 1}})).has_value());
  CHECK_FALSE(Decode(Encode(StatusVisuals{.records = {1, 1}})).has_value());
  StatusVisuals many;
  for (std::uint64_t i = 0; i <= kMaxStatusVisuals; ++i)
    many.records.push_back(i + 1);
  CHECK_FALSE(Decode(Encode(many)).has_value());
  many.records.pop_back();
  CHECK(Decode(Encode(many)).has_value());
  // A full set is still a small event.
  CHECK(Encode(many).size() < 160);
}

TEST_CASE("down-and-out messages survive encoding, reliably, and refuse what is out of range") {
  RoundTrips(Vitals{.player = 2, .revision = 7, .life = Life::Down, .flags = kVitalsInCombat});
  RoundTrips(Vitals{.player = 1, .revision = 1, .life = Life::Out});
  RoundTrips(Revive{.player = 1, .target = 2, .step = ReviveStep::Done});
  RoundTrips(GameOver{.round = 3});
  for (const Message message : {Message{Vitals{}}, Message{Revive{}}, Message{GameOver{}}}) {
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK_FALSE(IsStory(message));
  }
  CHECK(std::string(NameOf(Vitals{})) == "Vitals");
  CHECK(std::string(NameOf(GameOver{})) == "GameOver");
  CHECK_FALSE(Decode(Encode(Vitals{.life = static_cast<Life>(3)})).has_value());
  CHECK_FALSE(Decode(Encode(Revive{.step = static_cast<ReviveStep>(3)})).has_value());
  CHECK(Encode(Vitals{}).size() < 16);
}

TEST_CASE("consistency messages survive encoding, are small and reliable") {
  RoundTrips(Digest{.epoch = 3,
                    .seq = 9,
                    .world = 0x11111111,
                    .npcs = 0x22222222,
                    .story = 0x33333333,
                    .players = 0x44444444,
                    .npcCount = 12});
  RoundTrips(ResyncRequest{.domains = kDomainNpcs | kDomainStory});
  RoundTrips(NpcSet{.npcs = {1, 2, 70000}});
  RoundTrips(Epoch{
      .player = 2, .epoch = 4, .seenHost = 3, .reason = kEpochFastTravel, .firstNpcSeq = 1234});
  for (const Message message :
       {Message{Digest{}}, Message{ResyncRequest{}}, Message{NpcSet{}}, Message{Epoch{}}}) {
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK_FALSE(IsStory(message));
  }
  CHECK(std::string(NameOf(Digest{})) == "Digest");
  CHECK(std::string(NameOf(ResyncRequest{})) == "ResyncRequest");
  CHECK(std::string(NameOf(NpcSet{})) == "NpcSet");
  CHECK(std::string(NameOf(Epoch{})) == "Epoch");
  // Sent every few seconds to every guest: a few dozen bytes.
  CHECK(Encode(Digest{}).size() < 32);
  CHECK(Encode(Epoch{}).size() < 24);

  CHECK_FALSE(Decode(Encode(ResyncRequest{.domains = 0x80})).has_value());
  NpcSet big;
  big.npcs.resize(kMaxNpcSet + 1);
  CHECK_FALSE(Decode(Encode(big)).has_value());
  big.npcs.resize(kMaxNpcSet);
  CHECK(Decode(Encode(big)).has_value());
}

TEST_CASE("a V in another world: survives encoding, small, reliable, not a story message") {
  RoundTrips(Elsewhere{.player = 2,
                       .place = Place::Cyberspace,
                       .flags = kElsewhereMoved | kElsewhereSettled,
                       .anchor = {-1234.5f, 678.25f, 12.f},
                       .yaw = 1.5f});
  RoundTrips(Elsewhere{.player = 1, .place = Place::Johnny});
  CHECK(ChannelOf(Elsewhere{}) == Channel::Event);
  CHECK(IsReliable(Elsewhere{}));
  CHECK_FALSE(IsStory(Elsewhere{}));
  CHECK(std::string(NameOf(Elsewhere{})) == "Elsewhere");
  CHECK(Encode(Elsewhere{}).size() < 32);
  // A place this version does not know is refused.
  CHECK_FALSE(Decode(Encode(Elsewhere{.place = static_cast<Place>(5)})).has_value());
}

TEST_CASE("scene replay: survive encoding, story messages but the poses, bounded") {
  RoundTrips(SceneState{.scene = 0x1234567890abcdefull,
                        .play = 3,
                        .section = 17,
                        .startUs = 1'700'000'000'000'123ull,
                        .rate = 4.f,
                        .tier = 4,
                        .flags = kSceneSkipping | kSceneStaged,
                        .anchor = {-1500.5f, 1200.25f, 30.f},
                        .yaw = 1.25f,
                        .radius = 2.5f});
  RoundTrips(SceneState{.play = 3, .flags = kSceneInterrupted});
  RoundTrips(SceneCast{.play = 3, .actors = {{12, kActorPosed}, {70000, kActorGhost}}});
  RoundTrips(SceneLine{.play = 3,
                       .line = 0xfedcba9876543210ull,
                       .speaker = 12,
                       .startUs = 1'700'000'000'500'000ull,
                       .duration = 3.5f,
                       .flags = kVoiceHolocall});
  ActorPose pose{.npc = 12, .seq = 9, .timeUs = 1'700'000'000'000'000ull, .hips = {1, -2, 8192}};
  pose.rotations[5] = 77;
  RoundTrips(pose);
  for (const Message message :
       {Message{SceneState{}}, Message{SceneCast{}}, Message{SceneLine{}}}) {
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK(IsStory(message));
  }
  // A pose is state: late is worthless; it is not the story's path.
  CHECK(ChannelOf(ActorPose{}) == Channel::State);
  CHECK_FALSE(IsStory(ActorPose{}));
  CHECK_FALSE(IsStory(ShotHit{}));
  CHECK(std::string(NameOf(SceneState{})) == "SceneState");
  CHECK(std::string(NameOf(SceneCast{})) == "SceneCast");
  CHECK(std::string(NameOf(SceneLine{})) == "SceneLine");
  CHECK(std::string(NameOf(ActorPose{})) == "ActorPose");
  // Sent on changes, a few a second at most; a pose as big as a player's.
  CHECK(Encode(SceneState{}).size() < 64);
  CHECK(Encode(SceneLine{}).size() < 48);
  CHECK(Encode(ActorPose{}).size() <= Encode(PoseState{}).size() + 4);

  SceneCast crowd;
  crowd.actors.resize(kMaxSceneActors + 1);
  CHECK_FALSE(Decode(Encode(crowd)).has_value());
  crowd.actors.resize(kMaxSceneActors);
  CHECK(Decode(Encode(crowd)).has_value());
  CHECK_FALSE(Decode(Encode(SceneState{.rate = -1.f})).has_value());
  CHECK_FALSE(Decode(Encode(SceneState{.rate = 1000.f})).has_value());
  CHECK_FALSE(Decode(Encode(SceneState{.rate = std::nanf("")})).has_value());
}

TEST_CASE("scene replay, second part: props, the camera, rig poses and actor frames") {
  RoundTrips(SceneProp{.play = 3, .prop = 2, .record = 0x1122334455ull, .holder = 12});
  RoundTrips(SceneProp{
      .play = 3, .prop = 2, .record = 0x1122334455ull, .holderPlayer = 1, .flags = kPropLeftHand});
  RoundTrips(SceneProp{.play = 3, .prop = 2, .flags = kPropGone});
  CHECK(IsStory(SceneProp{}));
  CHECK(ChannelOf(SceneProp{}) == Channel::Event);
  CHECK(Encode(SceneProp{}).size() < 32);
  // No play, no number, flags of another version, two holders: refused.
  CHECK_FALSE(Decode(Encode(SceneProp{.prop = 1})).has_value());
  CHECK_FALSE(Decode(Encode(SceneProp{.play = 1})).has_value());
  CHECK_FALSE(Decode(Encode(SceneProp{.play = 1, .prop = 1, .flags = 8})).has_value());
  CHECK_FALSE(
      Decode(Encode(SceneProp{.play = 1, .prop = 1, .holder = 4, .holderPlayer = 2})).has_value());

  SceneCamera camera{.play = 3,
                     .seq = 40,
                     .timeUs = 1'700'000'000'000'000ull,
                     .position = {-1500.5f, 1200.25f, 30.f},
                     .fov = 51.f,
                     .flags = kCameraCut};
  camera.orientation = {1, 2, 3, 4, 5};
  RoundTrips(camera);
  CHECK(ChannelOf(SceneCamera{}) == Channel::State);
  CHECK_FALSE(IsStory(SceneCamera{}));
  CHECK(Encode(camera).size() <= 40);
  camera.fov = 0.f;
  CHECK_FALSE(Decode(Encode(camera)).has_value());
  camera.fov = std::nanf("");
  CHECK_FALSE(Decode(Encode(camera)).has_value());
  camera.fov = 60.f;
  camera.play = 0;
  CHECK_FALSE(Decode(Encode(camera)).has_value());

  // A rig pose: bone 3 at rest (no bytes), bone 9 turned (3), bone 300 whole (11).
  RigPose rig{.npc = 12, .part = kRigFace, .seq = 5, .timeUs = 77, .rig = 0xabcdef, .bones = 301};
  const auto entry = [&](std::uint16_t bone, std::uint8_t form, std::size_t bytes) {
    const auto head = static_cast<std::uint16_t>(bone | form << 14);
    rig.data.push_back(static_cast<std::uint8_t>(head & 0xFF));
    rig.data.push_back(static_cast<std::uint8_t>(head >> 8));
    rig.data.insert(rig.data.end(), bytes, std::uint8_t{7});
  };
  entry(3, kRigRest, 0);
  entry(9, kRigTurn, 3);
  entry(300, kRigWhole, 11);
  RoundTrips(rig);
  CHECK(ChannelOf(rig) == Channel::State);
  auto broken = rig;
  broken.data.pop_back(); // an entry cut short
  CHECK_FALSE(Decode(Encode(broken)).has_value());
  broken = rig;
  broken.bones = 300; // a bone the rig does not have
  CHECK_FALSE(Decode(Encode(broken)).has_value());
  broken = rig;
  entry(8, kRigShift, 6); // out of order
  CHECK_FALSE(Decode(Encode(rig)).has_value());
  broken.part = 2;
  CHECK_FALSE(Decode(Encode(broken)).has_value());
  broken = RigPose{.npc = 0, .bones = 4};
  CHECK_FALSE(Decode(Encode(broken)).has_value());

  // A rig's float tracks: track 2 at 0.5, track 40 at -1.
  RigTracks tracks{
      .npc = 12, .part = kRigFace, .seq = 6, .timeUs = 78, .rig = 0x1234, .tracks = 41};
  const auto track = [&](std::uint16_t index, std::int16_t steps) {
    tracks.data.push_back(static_cast<std::uint8_t>(index & 0xFF));
    tracks.data.push_back(static_cast<std::uint8_t>(index >> 8));
    const auto bits = static_cast<std::uint16_t>(steps);
    tracks.data.push_back(static_cast<std::uint8_t>(bits & 0xFF));
    tracks.data.push_back(static_cast<std::uint8_t>(bits >> 8));
  };
  track(2, 2048);
  track(40, -4096);
  RoundTrips(tracks);
  CHECK(ChannelOf(tracks) == Channel::State);
  CHECK(std::string(NameOf(RigTracks{})) == "RigTracks");
  auto badTracks = tracks;
  badTracks.data.pop_back(); // an entry cut short
  CHECK_FALSE(Decode(Encode(badTracks)).has_value());
  badTracks = tracks;
  badTracks.tracks = 40; // a track the rig does not have
  CHECK_FALSE(Decode(Encode(badTracks)).has_value());
  badTracks = tracks;
  badTracks.part = 2;
  CHECK_FALSE(Decode(Encode(badTracks)).has_value());
  badTracks = tracks;
  badTracks.npc = 0;
  CHECK_FALSE(Decode(Encode(badTracks)).has_value());
  track(1, 0); // out of order
  CHECK_FALSE(Decode(Encode(tracks)).has_value());

  // A prop set down.
  const ScenePlace place{.play = 2,
                         .prop = 3,
                         .entity = 0xE1,
                         .appearance = 0xA1,
                         .position = {1.f, 2.f, 3.f},
                         .orientation = {0.f, 0.f, 0.7071f, 0.7071f}};
  RoundTrips(place);
  CHECK(ChannelOf(place) == Channel::Event);
  CHECK(IsStory(place));
  CHECK(std::string(NameOf(ScenePlace{})) == "ScenePlace");
  RoundTrips(ScenePlace{.play = 2, .prop = 3, .flags = kPropGone}); // gone: no template needed
  auto badPlace = place;
  badPlace.entity = 0; // nothing to spawn
  CHECK_FALSE(Decode(Encode(badPlace)).has_value());
  badPlace = place;
  badPlace.prop = 0;
  CHECK_FALSE(Decode(Encode(badPlace)).has_value());
  badPlace = place;
  badPlace.flags = kPropLeftHand; // a hand: that is a SceneProp
  CHECK_FALSE(Decode(Encode(badPlace)).has_value());
  badPlace = place;
  badPlace.position.z = std::numeric_limits<float>::infinity();
  CHECK_FALSE(Decode(Encode(badPlace)).has_value());

  // An actor's frame is a pose frame with the NPC's name: the same rules.
  ActorFrame actor{.npc = 12};
  actor.frame.seq = 9;
  actor.frame.key = 4; // not a key: every bone kept
  RoundTrips(actor);
  CHECK(ChannelOf(actor) == Channel::State);
  actor.frame.rotations.push_back(1); // bytes no code accounts for
  CHECK_FALSE(Decode(Encode(actor)).has_value());
  CHECK_FALSE(Decode(Encode(ActorFrame{.npc = 0, .frame = {.seq = 9, .key = 4}})).has_value());
  CHECK(std::string(NameOf(ActorFrame{})) == "ActorFrame");
  CHECK(std::string(NameOf(RigPose{})) == "RigPose");
  CHECK(std::string(NameOf(SceneCamera{})) == "SceneCamera");
  CHECK(std::string(NameOf(SceneProp{})) == "SceneProp");
}

TEST_CASE("devices: survive encoding, small, reliable, and only known states pass") {
  RoundTrips(DeviceState{.entity = 0x1122334455667788ULL,
                         .cls = 0x99AABBCCDDEEFF00ULL,
                         .status = kDeviceUnpowered,
                         .flags = kDeviceOpen | kDeviceHacked | kDeviceGlitching,
                         .value = 12,
                         .revision = 7,
                         .by = 3,
                         .how = kDeviceQuiet});
  RoundTrips(DeviceState{.entity = 1, .cls = 2, .status = kDeviceDisabled, .how = kDeviceAsk});
  RoundTrips(DeviceUse{.entity = 0x1122334455667788ULL,
                       .cls = 0x99AABBCCDDEEFF00ULL,
                       .status = kDeviceOn,
                       .flags = kDeviceLocked,
                       .value = -1,
                       .seen = 4,
                       .player = 2});
  RoundTrips(DeviceSet{.count = 300});
  RoundTrips(DeviceDigest{.count = 300, .hash = 0xDEADBEEF});
  for (const Message &message : {Message{DeviceState{}}, Message{DeviceUse{}}, Message{DeviceSet{}},
                                 Message{DeviceDigest{}}}) {
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK(IsReliable(message));
    CHECK_FALSE(IsStory(message));
    CHECK_FALSE(IsCombat(message));
  }
  CHECK(std::string(NameOf(DeviceState{})) == "DeviceState");
  CHECK(std::string(NameOf(DeviceUse{})) == "DeviceUse");
  CHECK(std::string(NameOf(DeviceSet{})) == "DeviceSet");
  CHECK(std::string(NameOf(DeviceDigest{})) == "DeviceDigest");
  // A whole record goes to a late joiner: each device is a few dozen bytes.
  CHECK(Encode(DeviceState{}).size() <= 36);
  CHECK(Encode(DeviceUse{}).size() <= 36);
  CHECK(Encode(DeviceDigest{}).size() <= 12);
  // A status, a state bit or a way of travelling this version does not know.
  CHECK_FALSE(Decode(Encode(DeviceState{.status = 2})).has_value());
  CHECK_FALSE(Decode(Encode(DeviceState{.status = -3})).has_value());
  CHECK_FALSE(Decode(Encode(DeviceState{.flags = 0x800})).has_value());
  CHECK_FALSE(Decode(Encode(DeviceState{.how = 3})).has_value());
  CHECK_FALSE(Decode(Encode(DeviceUse{.flags = 0x8000})).has_value());
  CHECK_FALSE(Decode(Encode(DeviceUse{.status = 5})).has_value());
}

TEST_CASE("breaches: survive encoding, reliable one-shots, bounded, known flags only") {
  RoundTrips(Breach{.player = 2,
                    .entity = 0x1122334455667788ULL,
                    .cls = 0x99AABBCCDDEEFF00ULL,
                    .npc = 7,
                    .programs = {1, 2, 3},
                    .traps = {4},
                    .flags = kBreachRemote | kBreachApply});
  RoundTrips(Breach{.entity = 1, .cls = 2, .flags = kBreachFailed});
  RoundTrips(
      BreachResult{.entity = 1, .cls = 2, .npc = 3, .verdict = BreachVerdict::Breached, .by = 4});
  for (const Message &message : {Message{Breach{}}, Message{BreachResult{}}}) {
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK(IsReliable(message));
    CHECK_FALSE(IsStory(message));
    CHECK_FALSE(IsCombat(message));
    CHECK_FALSE(IsVehicleCombat(message));
  }
  CHECK(std::string(NameOf(Breach{})) == "Breach");
  CHECK(std::string(NameOf(BreachResult{})) == "BreachResult");
  // A full grid of daemons is still a small message.
  Breach full{.entity = 1, .cls = 2};
  full.programs.assign(kMaxBreachPrograms, 0xFFFFFFFFFFull);
  CHECK(Encode(full).size() <= 200);
  CHECK(Encode(BreachResult{}).size() <= 32);
  // Past the bounds, a flag or a verdict this version does not know.
  auto many = full;
  many.programs.push_back(1);
  CHECK_FALSE(Decode(Encode(many)).has_value());
  auto traps = full;
  traps.traps.assign(kMaxBreachPrograms + 1, 1);
  CHECK_FALSE(Decode(Encode(traps)).has_value());
  CHECK_FALSE(Decode(Encode(Breach{.flags = 8})).has_value());
  CHECK_FALSE(Decode(Encode(BreachResult{.verdict = BreachVerdict{3}})).has_value());
}

TEST_CASE("the world's objects: fractures and a device's fire survive encoding, small, known "
          "kinds only") {
  RoundTrips(Fracture{.player = 2,
                      .entity = 0x1122334455667788ULL,
                      .component = 0x99AABBCCDDEEFF00ULL,
                      .at = {1.5f, -2.f, 30.f},
                      .level = 2});
  RoundTrips(Fracture{.at = {4.f, 5.f, 6.f}});
  RoundTrips(DeviceFire{.entity = 7,
                        .kind = DeviceFireKind::Shot,
                        .target = {TargetKind::Player, 2},
                        .at = {1.f, 2.f, 3.f},
                        .timeUs = 123456789});
  RoundTrips(DeviceFire{.entity = 7, .kind = DeviceFireKind::Lost, .player = 3});
  CHECK(Message(Fracture{}).index() == Message(SceneProp{}).index() + 1); // appended
  CHECK(Message(DeviceFire{}).index() == Message(Fracture{}).index() + 1);
  CHECK(std::string(NameOf(Fracture{})) == "Fracture");
  CHECK(std::string(NameOf(DeviceFire{})) == "DeviceFire");
  CHECK(ChannelOf(Fracture{}) == Channel::Event);
  // A shot at a V is fired for real by that V's game: it must arrive. A
  // shot at anyone else is drawn, and of the moment.
  CHECK(ChannelOf(DeviceFire{.kind = DeviceFireKind::Shot, .target = {TargetKind::Player, 2}}) ==
        Channel::Event);
  CHECK(ChannelOf(DeviceFire{.kind = DeviceFireKind::Shot, .target = {TargetKind::Npc, 9}}) ==
        Channel::State);
  CHECK(ChannelOf(DeviceFire{.kind = DeviceFireKind::Shot}) == Channel::State);
  for (const auto kind :
       {DeviceFireKind::Idle, DeviceFireKind::Aim, DeviceFireKind::Seen, DeviceFireKind::Lost})
    CHECK(ChannelOf(DeviceFire{.kind = kind}) == Channel::Event);
  // A turret's burst is ten of these a second.
  CHECK(Encode(Fracture{}).size() <= 36);
  CHECK(Encode(DeviceFire{}).size() <= 48);
  CHECK_FALSE(Decode(Encode(DeviceFire{.kind = static_cast<DeviceFireKind>(5)})).has_value());
  CHECK_FALSE(Decode(Encode(DeviceFire{.target = {static_cast<TargetKind>(4), 1}})).has_value());
}

TEST_CASE("what a proxy cannot do survives encoding, for its owner: reliable, small, combat") {
  RoundTrips(
      WeakSpotHit{.player = 2, .npc = 7, .record = 0x1122334455ULL, .index = 1, .drain = 35.5f});
  RoundTrips(HackTraced{.player = 3, .npc = 9});
  CHECK(Message(WeakSpotHit{}).index() == Message(StoryGates{}).index() + 1); // appended
  CHECK(Message(HackTraced{}).index() == Message(WeakSpotHit{}).index() + 1);
  CHECK(std::string(NameOf(WeakSpotHit{})) == "WeakSpotHit");
  CHECK(std::string(NameOf(HackTraced{})) == "HackTraced");
  CHECK(ChannelOf(WeakSpotHit{}) == Channel::Event);
  CHECK(ChannelOf(HackTraced{}) == Channel::Event);
  CHECK(IsReliable(WeakSpotHit{}));
  CHECK(IsCombat(WeakSpotHit{}));
  CHECK(IsCombat(HackTraced{}));
  CHECK(Encode(WeakSpotHit{}).size() <= 32);
  CHECK(Encode(HackTraced{}).size() <= 16);
  // An NPC's health: protected or not, nothing this version does not know.
  CHECK_FALSE(Decode(Encode(NpcHealth{.npc = 1, .flags = 2})).has_value());
}

TEST_CASE("a turret in a player's hands survives encoding, small, unreliable, known flags "
          "only") {
  RoundTrips(TurretHands{
      .player = 2, .entity = 0x1122334455667788ULL, .at = {1.f, -2.f, 3.f}, .flags = kHandsFiring});
  RoundTrips(TurretHands{.entity = 7, .flags = kHandsLetGo});
  CHECK(Message(TurretHands{}).index() == Message(RigTracks{}).index() + 1); // appended
  CHECK(std::string(NameOf(TurretHands{})) == "TurretHands");
  CHECK(ChannelOf(TurretHands{}) == Channel::State);
  CHECK_FALSE(IsReliable(TurretHands{}));
  CHECK(Encode(TurretHands{}).size() <= 32);
  CHECK_FALSE(Decode(Encode(TurretHands{.flags = 4})).has_value());
}

TEST_CASE("a turret's refused claim survives encoding: reliable, small, appended") {
  RoundTrips(TurretBusy{.entity = 0x1122334455667788ULL, .holder = 3});
  CHECK(Message(TurretBusy{}).index() == Message(HackTraced{}).index() + 1); // appended
  CHECK(std::string(NameOf(TurretBusy{})) == "TurretBusy");
  CHECK(ChannelOf(TurretBusy{}) == Channel::Event);
  CHECK(IsReliable(TurretBusy{}));
  CHECK_FALSE(IsCombat(TurretBusy{}));
  CHECK(Encode(TurretBusy{}).size() <= 16);
}

TEST_CASE("a quickhack on a shared vehicle survives encoding: reliable, small, appended, known "
          "hacks only") {
  RoundTrips(
      TrafficHack{.player = 2, .id = 0x0200'0007, .hack = CarHack::Accelerate, .seconds = 4.5f});
  CHECK(Message(TrafficHack{}).index() == Message(TurretBusy{}).index() + 1); // appended
  CHECK(std::string(NameOf(TrafficHack{})) == "TrafficHack");
  CHECK(ChannelOf(TrafficHack{}) == Channel::Event);
  CHECK(IsReliable(TrafficHack{}));
  CHECK_FALSE(IsCombat(TrafficHack{}));
  CHECK(Encode(TrafficHack{}).size() <= 16);
  CHECK_FALSE(Decode(Encode(TrafficHack{.hack = static_cast<CarHack>(3)})).has_value());
}

TEST_CASE("a car's paint survives encoding: reliable, small, appended, one form per paint") {
  const CarPaint twoTone{.player = 2,
                         .vehicle = 0x2a1b,
                         .flags = kPaintPrimary | kPaintSecondary | kPaintLights,
                         .primaryR = 200,
                         .primaryG = 10,
                         .primaryB = 30,
                         .secondaryR = 1,
                         .secondaryG = 2,
                         .secondaryB = 3,
                         .lightsHue = 215.5f};
  RoundTrips(twoTone);
  RoundTrips(
      CarPaint{.player = 3, .vehicle = 7, .flags = kPaintUnique, .unique = 0x51, .model = 0x77});
  RoundTrips(CarPaint{.player = 3}); // stock paint
  RoundTrips(CarPaint{.vehicle = 7, .flags = kPaintLights, .lightsHue = 360.f});
  CHECK(Message(CarPaint{}).index() == Message(TrafficHack{}).index() + 1); // appended
  CHECK(std::string(NameOf(CarPaint{})) == "CarPaint");
  CHECK(ChannelOf(CarPaint{}) == Channel::Event);
  CHECK(IsReliable(CarPaint{}));
  CHECK(IsCombat(CarPaint{}));
  CHECK_FALSE(IsStory(CarPaint{}));
  CHECK(Encode(twoTone).size() <= 48);
  // Refused on the wire: a flag of another version, a template with colours
  // or without its record, a second colour alone, a colour or a hue nothing
  // announces, a hue that is no angle, a paint on no car.
  const auto refused = [](const CarPaint &paint) { return !Decode(Encode(paint)).has_value(); };
  CHECK(refused({.vehicle = 7, .flags = 16}));
  CHECK(refused({.vehicle = 7, .flags = kPaintUnique | kPaintPrimary, .unique = 5}));
  CHECK(refused({.vehicle = 7, .flags = kPaintUnique}));
  CHECK(refused({.vehicle = 7, .flags = kPaintPrimary, .unique = 5}));
  CHECK(refused({.vehicle = 7, .flags = kPaintSecondary}));
  CHECK(refused({.vehicle = 7, .flags = kPaintLights, .primaryG = 1, .lightsHue = 10.f}));
  CHECK(refused({.vehicle = 7, .flags = kPaintPrimary, .secondaryB = 1}));
  CHECK(refused({.vehicle = 7, .flags = kPaintPrimary, .lightsHue = 10.f}));
  CHECK(refused({.vehicle = 7, .flags = kPaintLights, .lightsHue = 361.f}));
  CHECK(refused({.vehicle = 7, .flags = kPaintLights, .lightsHue = -1.f}));
  CHECK(refused({.flags = kPaintPrimary}));
}

TEST_CASE("a hit a V took survives encoding: of the moment, small, appended, checked") {
  const HitTaken said{.player = 2,
                      .by = HitBy::Npc,
                      .attacker = 7,
                      .attack = 0x1122334455667788ULL,
                      .attackType = 10,
                      .flags = kHitTakenStaggered | kHitTakenCritical,
                      .zone = 8,
                      .shape = 0,
                      .share = 64,
                      .offset = {-0.2f, 0.1f, 0.5f},
                      .direction = {0.6f, -0.8f, 0.f}};
  // The wire's form: its place within 2 cm an axis, its way within a degree.
  const auto wire = Snapped(said);
  RoundTrips(wire);
  CHECK(Snapped(wire) == wire);
  CHECK(wire.offset.x == doctest::Approx(-0.2f).epsilon(0.1));
  CHECK(wire.offset.z == doctest::Approx(0.5f).epsilon(0.05));
  CHECK(wire.direction.y == doctest::Approx(-0.8f).epsilon(0.01));
  CHECK(wire.attack == said.attack);
  // Off the body: clamped to its reach; not a number: none.
  const auto far = Snapped(HitTaken{
      .offset = {9.f, 0.f, 0.f}, .direction = {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}});
  CHECK(far.offset.x == kHitTakenReach);
  CHECK(far.direction.x == 0.f);
  RoundTrips(HitTaken{.by = HitBy::Player, .attacker = 3, .flags = kHitTakenMelee});
  RoundTrips(HitTaken{}); // nobody the others know, no hit shape
  CHECK(Message(HitTaken{}).index() == Message(CarPaint{}).index() + 1); // appended
  CHECK(std::string(NameOf(HitTaken{})) == "HitTaken");
  CHECK(ChannelOf(HitTaken{}) == Channel::State);
  CHECK_FALSE(IsReliable(HitTaken{}));
  CHECK_FALSE(IsCombat(HitTaken{}));
  CHECK_FALSE(IsStory(HitTaken{}));
  CHECK(Encode(HitTaken{}).size() == 26);
  // An attacker of no known kind, one named for nobody or not named, a flag
  // this version does not know.
  const auto refused = [](const HitTaken &hit) { return !Decode(Encode(hit)).has_value(); };
  CHECK(refused({.by = static_cast<HitBy>(3), .attacker = 1}));
  CHECK(refused({.by = HitBy::World, .attacker = 7}));
  CHECK(refused({.by = HitBy::Npc}));
  CHECK(refused({.flags = 16}));
}

TEST_CASE("vehicles changing hands: survive encoding, reliable, small") {
  RoundTrips(TrafficOwnership{.id = MakeTrafficId(2, 9), .owner = 3});
  RoundTrips(TrafficHandover{.id = MakeTrafficId(2, 9), .to = 3});
  RoundTrips(TrafficHandoverState{.id = MakeTrafficId(2, 9),
                                  .from = 2,
                                  .to = 3,
                                  .position = {1, 2, 3},
                                  .orientation = {0, 0, 0.6f, 0.8f},
                                  .velocity = {4, 5, 6},
                                  .health = 0.5f,
                                  .flags = kTrafficLights,
                                  .handover = kHandoverInherited});
  RoundTrips(TrafficHandoverDone{.id = MakeTrafficId(2, 9), .accepted = true});
  for (const Message &message : {Message{TrafficOwnership{}}, Message{TrafficHandover{}},
                                 Message{TrafficHandoverState{}}, Message{TrafficHandoverDone{}}}) {
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK(IsReliable(message));
    CHECK_FALSE(IsStory(message));
    CHECK_FALSE(IsCombat(message));
    CHECK(Encode(message).size() <= 80);
  }
  CHECK(std::string(NameOf(TrafficOwnership{})) == "TrafficOwnership");
  CHECK(std::string(NameOf(TrafficHandover{})) == "TrafficHandover");
  CHECK(std::string(NameOf(TrafficHandoverState{})) == "TrafficHandoverState");
  CHECK(std::string(NameOf(TrafficHandoverDone{})) == "TrafficHandoverDone");
}

TEST_CASE("region messages survive encoding, are reliable and bounded") {
  RoundTrips(Regions{.revision = 4, .pairs = {1, 2, 2, 3}});
  RoundTrips(NpcOwner{.npc = 0x01000007, .owner = 3, .flags = kNpcPinned});
  RoundTrips(NpcHandover{.npc = 9, .to = 2});
  RoundTrips(NpcHandoverState{.npc = 9,
                              .from = 1,
                              .to = 2,
                              .position = {1.f, 2.f, 3.f},
                              .yaw = 0.5f,
                              .health = 0.4f,
                              .poise = 0.9f,
                              .flags = kHandoverCombat,
                              .target = 2,
                              .hostileTo = 3,
                              .highLevel = 2,
                              .effects = {{.record = 77, .remaining = 3.5f, .stacks = 2}}});
  RoundTrips(NpcHandoverDone{.npc = 9, .accepted = true});
  RoundTrips(OwnedDigest{.seq = 5, .npcs = 0xABCDEF01, .count = 12});
  RoundTrips(RegionHold{.player = 3, .held = true});
  for (const Message message : {Message{Regions{}}, Message{NpcOwner{}}, Message{NpcHandover{}},
                                Message{NpcHandoverState{}}, Message{NpcHandoverDone{}},
                                Message{OwnedDigest{}}, Message{RegionHold{}}}) {
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK_FALSE(IsStory(message));
    CHECK_FALSE(IsCombat(message));
  }
  CHECK(std::string(NameOf(Regions{})) == "Regions");
  CHECK(std::string(NameOf(NpcHandoverState{})) == "NpcHandoverState");
  CHECK(std::string(NameOf(OwnedDigest{})) == "OwnedDigest");
  // An odd list is not pairs; a state carries a bounded list of effects.
  CHECK_FALSE(Decode(Encode(Regions{.pairs = {1, 2, 3}})).has_value());
  NpcHandoverState big;
  big.effects.resize(kMaxNpcEffects + 1);
  CHECK_FALSE(Decode(Encode(big)).has_value());
  // Appended after DeviceDigest (the version moved for NpcCueKind::OutOfSight,
  // not for these).
  CHECK(Message(Regions{}).index() == Message(DeviceDigest{}).index() + 1);
}

TEST_CASE("presence, cues and NPC reactions round-trip, on the right channels, and stay small") {
  RoundTrips(Presence{
      .player = 2, .revision = 9, .health = 128, .flags = kPresenceMenu | kPresenceCombat});
  RoundTrips(Cue{.player = 3,
                 .seq = 77,
                 .kind = CueKind::Trace,
                 .arg = kTraceLeftHand | kTraceCharacter,
                 .at = {1.5f, -2.5f, 30.f}});
  RoundTrips(Cue{.player = 1, .seq = 1, .kind = CueKind::MeleeHit, .at = {4.f, 5.f, 6.f}});
  // A ricochet's leg and a charge's step (v13).
  RoundTrips(Cue{.player = 3,
                 .seq = 78,
                 .kind = CueKind::Ricochet,
                 .arg = kTraceCharacter,
                 .at = {2.f, -2.f, 31.f}});
  RoundTrips(Cue{.player = 3, .seq = 79, .kind = CueKind::Charge, .arg = kChargeFull});
  // The left hand's charge (v16): the hand in `at.y`.
  RoundTrips(Cue{.player = 3,
                 .seq = 80,
                 .kind = CueKind::Charge,
                 .arg = kChargeCharging,
                 .at = {0.4f, kChargeLeftHand, 0.f}});
  // A charge's level and hand in a byte each (v17), the other kinds' place
  // whole.
  for (const float level : {0.f, 0.3f, 0.6f, 1.f, 7.f, -1.f}) {
    const Cue charge{.player = 8,
                     .seq = 80,
                     .kind = CueKind::Charge,
                     .arg = kChargeCharging,
                     .at = {ChargeLevel(level), kChargeLeftHand, 0.f}};
    RoundTrips(charge);
    CHECK(charge.at.x >= 0.f);
    CHECK(charge.at.x <= 1.f);
    CHECK(std::abs(charge.at.x - std::clamp(level, 0.f, 1.f)) <= 0.5f / kChargeLevels + 1e-6f);
  }
  CHECK(ChargeLevel(std::nanf("")) == 0.f);
  CHECK(Encode(Cue{.kind = CueKind::Charge}).size() == 10);
  CHECK(Encode(Cue{.kind = CueKind::Trace}).size() == 20);
  const NpcReaction stagger{.npc = 12,
                            .by = 2,
                            .type = 3,
                            .direction = 3,
                            .intensity = 2,
                            .bodyPart = 4,
                            .variation = 1,
                            .source = -1,
                            .stance = 1,
                            .moveSpeed = 2,
                            .moveDirection = 1,
                            .flags = kReactionTurns,
                            .angle = 91.5f,
                            .way = {0.f, -1.f, 0.f}};
  RoundTrips(stagger);

  // Health and the menu are state: kept and never dropped. A cue is of the
  // moment, one per shot: unreliable and small.
  CHECK(ChannelOf(Presence{}) == Channel::Event);
  CHECK(IsReliable(Presence{}));
  CHECK(ChannelOf(Cue{.kind = CueKind::Trace}) == Channel::State);
  CHECK(Encode(Cue{.kind = CueKind::Trace}).size() < 32);
  CHECK(Encode(Presence{}).size() < 16);
  // A twitch is one of many; a knockdown, a death or a ragdoll must arrive.
  CHECK(ChannelOf(NpcReaction{.type = kReactionTwitch}) == Channel::State);
  CHECK(ChannelOf(stagger) == Channel::Event);
  CHECK(IsReliable(NpcReaction{.type = kReactionDeath}));
  CHECK(IsReliable(NpcReaction{.type = kReactionRagdoll}));
  CHECK(IsReliable(NpcReaction{.type = kReactionRagdollEnd}));
  CHECK(Encode(stagger).size() < 40);
  CHECK_FALSE(IsStory(Presence{}));
  CHECK_FALSE(IsCombat(NpcReaction{}));

  // Kinds the receiver does not know are refused, not guessed at.
  CHECK_FALSE(Decode(Encode(Cue{.kind = static_cast<CueKind>(0)})).has_value());
  CHECK_FALSE(Decode(Encode(Cue{.kind = static_cast<CueKind>(5)})).has_value());
  CHECK(Decode(Encode(Cue{.kind = CueKind::Charge, .arg = kChargeMax})).has_value());
  CHECK_FALSE(Decode(Encode(Cue{.kind = CueKind::Charge, .arg = kChargeMax + 1})).has_value());
  CHECK_FALSE(Decode(Encode(Cue{.kind = CueKind::Trace, .arg = 8})).has_value());
  CHECK(Encode(Cue{.kind = CueKind::Charge}).size() < 32);
  CHECK_FALSE(Decode(Encode(NpcReaction{.type = kReactionMax + 1})).has_value());
  CHECK(std::string_view(NameOf(Presence{})) == "Presence");
  CHECK(std::string_view(NameOf(Cue{})) == "Cue");
  CHECK(std::string_view(NameOf(NpcReaction{})) == "NpcReaction");
}

TEST_CASE("a body's rest is the corpse owner's reliable word, its ragdoll's bodies") {
  CorpsePose rest{.npc = 7, .position = {4.f, 5.f, 6.f}, .yaw = 1.5f};
  for (std::int16_t i = 0; i < 20; ++i)
    rest.bodies.push_back({.offset = {i, std::int16_t(-i), 30}, .rotation = {0, 0, i, 32767}});
  RoundTrips(rest);
  CHECK(ChannelOf(rest) == Channel::Event);
  CHECK(IsCombat(rest));
  CHECK_FALSE(tandem::consistency::IsOneShot(rest));
  // A humanoid's ragdoll (20 bodies or so) in one datagram, as small as a wound list.
  CHECK(Encode(rest).size() < 320);
  rest.bodies.resize(kMaxRagdollBodies);
  RoundTrips(rest);
  CHECK(Encode(rest).size() < 500);
  CHECK(std::string_view(NameOf(rest)) == "CorpsePose");
  // No NPC, a place or heading that is not a number, no bodies, too many: refused.
  CHECK_FALSE(Decode(Encode(CorpsePose{})).has_value());
  auto lost = rest;
  lost.yaw = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(Decode(Encode(lost)).has_value());
  auto empty = rest;
  empty.bodies.clear();
  CHECK_FALSE(Decode(Encode(empty)).has_value());
  auto many = rest;
  many.bodies.resize(kMaxRagdollBodies + 1);
  CHECK_FALSE(Decode(Encode(many)).has_value());
}

TEST_CASE("an NPC's voice line is a small reliable one-shot with a known context") {
  const NpcVoice line{
      .npc = 12, .context = 0x1234'5678'9abc'def0ULL, .delayMs = 500, .flags = kVoiceInVehicle};
  RoundTrips(line);
  // After its NPC's spawn, on the same channel: the receiver knows whose.
  CHECK(ChannelOf(line) == Channel::Event);
  CHECK(Encode(line).size() < 24);
  CHECK(tandem::consistency::IsOneShot(line));
  CHECK(std::string_view(NameOf(line)) == "NpcVoice");
  // No context, a delay past its bound, flags this version does not know.
  CHECK_FALSE(Decode(Encode(NpcVoice{.npc = 12})).has_value());
  CHECK_FALSE(Decode(Encode(NpcVoice{.npc = 12, .context = 1, .delayMs = kNpcVoiceMaxDelayMs + 1}))
                  .has_value());
  CHECK_FALSE(Decode(Encode(NpcVoice{.npc = 12, .context = 1, .flags = 2})).has_value());
}

TEST_CASE("a summoned car survives encoding: reliable, small, appended, one form for none") {
  const OwnCar arriving{.player = 2,
                        .vehicle = 0x2a1b,
                        .flags = kOwnCarMoving,
                        .position = {100.f, -20.f, 7.5f},
                        .orientation = {0.f, 0.f, 0.7071f, 0.7071f},
                        .hint = {.type = 0x55, .size = 2}};
  RoundTrips(arriving);
  RoundTrips(OwnCar{.player = 2, .vehicle = 0x2a1b, .position = {4.f, 5.f, 6.f}}); // parked
  RoundTrips(OwnCar{.player = 3});                                                 // none out
  CHECK(Message(OwnCar{}).index() == Message(CorpsePose{}).index() + 1);           // appended
  CHECK(std::string(NameOf(OwnCar{})) == "OwnCar");
  CHECK(ChannelOf(OwnCar{}) == Channel::Event);
  CHECK(IsReliable(OwnCar{}));
  CHECK(IsCombat(OwnCar{}));
  CHECK_FALSE(IsStory(OwnCar{}));
  CHECK_FALSE(tandem::consistency::IsOneShot(OwnCar{}));
  CHECK(Encode(arriving).size() <= 64);
  // Refused on the wire: a flag of another version, no car with anything
  // else said, a place that is no number.
  const auto refused = [](const OwnCar &car) { return !Decode(Encode(car)).has_value(); };
  CHECK(refused({.vehicle = 7, .flags = 4}));
  CHECK(refused({.flags = kOwnCarMoving}));
  CHECK(refused({.position = {1.f, 0.f, 0.f}}));
  CHECK(refused({.vehicle = 7, .position = {std::numeric_limits<float>::infinity(), 0.f, 0.f}}));
  CHECK(refused(
      {.vehicle = 7, .orientation = {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 1.f}}));
}

TEST_CASE("throws, their ends, the session's rules and a defence survive encoding") {
  RoundTrips(Throw{.player = 2,
                   .id = 9,
                   .kind = ThrowKind::Grenade,
                   .flags = kThrowSticky | kThrowCharged,
                   .item = 0x1122334455ULL,
                   .origin = {1, 2, 3},
                   .velocity = {4, 5, 6},
                   .npc = 7});
  RoundTrips(Throw{.player = 3, .id = 1, .kind = ThrowKind::Smart, .at = 1});
  // A smart round of the left hand's weapon (v16).
  RoundTrips(Throw{.player = 3,
                   .id = 2,
                   .kind = ThrowKind::Smart,
                   .flags = kThrowHoming | kThrowLeftHand,
                   .npc = 7});
  RoundTrips(ThrowEnd{.player = 2, .id = 9, .how = ThrowEndKind::Stuck, .position = {1, 2, 3}});
  RoundTrips(Rules{.flags = kRuleFriendlyFire});
  RoundTrips(Rules{.flags = kRuleFriendlyFire | kRuleEnemyScaling, .enemyHealth = 85});
  // The host's level_gap (v24): off, and its largest.
  RoundTrips(Rules{.flags = kRuleEnemyScaling, .levelGap = 0});
  RoundTrips(Rules{.levelGap = kMaxLevelGap});
  CHECK(Rules{}.enemyHealth == 60);
  CHECK(Rules{}.levelGap == 8);
  RoundTrips(Defended{.player = 2, .npc = 7, .how = DefendKind::Parried});
  CHECK(std::string_view(NameOf(Throw{})) == "Throw");
  CHECK(std::string_view(NameOf(ThrowEnd{})) == "ThrowEnd");
  CHECK(std::string_view(NameOf(Rules{})) == "Rules");
  CHECK(std::string_view(NameOf(Defended{})) == "Defended");
  // One path for hit outcomes in the endpoint.
  for (const auto &message :
       {Message(Throw{}), Message(ThrowEnd{}), Message(Rules{}), Message(Defended{})})
    CHECK(IsCombat(message));
  CHECK_FALSE(IsCombat(ShotHit{}));
  CHECK_FALSE(IsCombat(NpcShotAt{}));
  // A grenade's flight is state the others must see once; a smart round is
  // one per shot, like the shot.
  CHECK(ChannelOf(Throw{.kind = ThrowKind::Grenade}) == Channel::Event);
  CHECK(ChannelOf(Throw{.kind = ThrowKind::Knife}) == Channel::Event);
  CHECK(ChannelOf(Throw{.kind = ThrowKind::Smart}) == Channel::State);
  CHECK(IsReliable(ThrowEnd{}));
  CHECK(IsReliable(Rules{}));
  CHECK(IsReliable(Defended{}));
  // Smart rounds go with every shot: small.
  CHECK(Encode(Throw{}).size() < 64);
}

TEST_CASE("a thrown object's path round-trips, small and unreliable") {
  RoundTrips(ThrowPath{.player = 2, .id = 9, .position = {1, 2, 3}, .velocity = {4, -5, 6}});
  RoundTrips(ThrowPath{.player = 3, .id = 1, .position = {7, 8, 9}}); // lying still
  CHECK(std::string_view(NameOf(ThrowPath{})) == "ThrowPath");
  CHECK(IsCombat(ThrowPath{}));
  // A late one is worth nothing: the next, or the throw's end, says more.
  CHECK(ChannelOf(ThrowPath{}) == Channel::State);
  CHECK_FALSE(IsReliable(ThrowPath{}));
  CHECK(Encode(ThrowPath{}).size() <= 40);
}

TEST_CASE("throws, rules and defences of kinds the receiver does not know are refused") {
  auto bytes = Encode(Throw{.kind = static_cast<ThrowKind>(4)});
  CHECK_FALSE(Decode(bytes).has_value());
  bytes = Encode(ThrowEnd{.how = static_cast<ThrowEndKind>(3)});
  CHECK_FALSE(Decode(bytes).has_value());
  bytes = Encode(Rules{.flags = 4});
  CHECK_FALSE(Decode(bytes).has_value());
  bytes = Encode(Rules{.flags = kRuleEnemyScaling, .enemyHealth = kMaxEnemyHealth + 1});
  CHECK_FALSE(Decode(bytes).has_value());
  bytes = Encode(Rules{.levelGap = kMaxLevelGap + 1});
  CHECK_FALSE(Decode(bytes).has_value());
  bytes = Encode(Defended{.how = static_cast<DefendKind>(0)});
  CHECK_FALSE(Decode(bytes).has_value());
  bytes = Encode(Defended{.how = static_cast<DefendKind>(3)});
  CHECK_FALSE(Decode(bytes).has_value());
}

TEST_CASE("activities and time skips: survive encoding, small, reliable, unknown kinds refused") {
  RoundTrips(Activity{.player = 2,
                      .kind = ActivityKind::Workspot,
                      .flags = kActivityAnchored | kActivityLying,
                      .device = 0x1122334455667788ull,
                      .workspot = 0x99AABBCCDDEEFF00ull,
                      .entry = 7,
                      .anim = 0x1234ull,
                      .anchor = {-12.5f, 300.f, 7.25f},
                      .yaw = -1.25f});
  RoundTrips(Activity{.player = 1, .kind = ActivityKind::Photo, .flags = kActivityFrozen});
  RoundTrips(TimeSkip{.from = 3,
                      .request = 4,
                      .reason = SkipReason::Sleep,
                      .fromSeconds = 3600,
                      .toSeconds = 8 * 3600});
  RoundTrips(
      TimeSkipVote{.from = 1, .asker = 3, .request = 4, .accepted = false, .why = kSkipCombat});
  RoundTrips(TimeSkipResult{.asker = 3,
                            .request = 4,
                            .reason = SkipReason::Wait,
                            .outcome = SkipOutcome::Declined,
                            .by = 1,
                            .why = kSkipCombat,
                            .fromSeconds = 3600,
                            .toSeconds = 7200});
  for (const Message m : {Message(Activity{}), Message(TimeSkip{}), Message(TimeSkipVote{}),
                          Message(TimeSkipResult{})}) {
    CHECK(ChannelOf(m) == Channel::Event);
    CHECK_FALSE(IsStory(m));
    CHECK(Encode(m).size() < 64);
  }
  CHECK(std::string(NameOf(Activity{})) == "Activity");
  CHECK(std::string(NameOf(TimeSkip{})) == "TimeSkip");
  CHECK(std::string(NameOf(TimeSkipVote{})) == "TimeSkipVote");
  CHECK(std::string(NameOf(TimeSkipResult{})) == "TimeSkipResult");
  // What this version does not know is refused.
  CHECK_FALSE(Decode(Encode(Activity{.kind = static_cast<ActivityKind>(9)})).has_value());
  CHECK_FALSE(Decode(Encode(TimeSkip{.reason = static_cast<SkipReason>(3)})).has_value());
  CHECK_FALSE(Decode(Encode(TimeSkipVote{.why = kSkipWhyMax + 1})).has_value());
  CHECK_FALSE(Decode(Encode(TimeSkipResult{.outcome = static_cast<SkipOutcome>(5)})).has_value());
}

TEST_CASE("levels survive encoding, are small reliable state, and refuse what is out of range") {
  RoundTrips(PlayerLevel{.player = 2, .level = 37});
  RoundTrips(SessionLevel{.level = 30, .players = 2});
  RoundTrips(NpcLevel{.npc = 7, .level = 28.5f});
  for (const Message message :
       {Message(PlayerLevel{.level = 1}), Message(SessionLevel{.level = 1, .players = 1}),
        Message(NpcLevel{})}) {
    // They travel the hit outcomes' path (Endpoint::SendCombat).
    CHECK(IsCombat(message));
    CHECK_FALSE(IsStory(message));
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK(Encode(message).size() < 16);
  }
  // Appended after every older message: no index moved.
  CHECK(Message(PlayerLevel{}).index() == Message(TimeSkipResult{}).index() + 1);
  CHECK(Message(Trade{}).index() == Message(NpcLevel{}).index() + 1);
  CHECK(std::string(NameOf(PlayerLevel{})) == "PlayerLevel");
  CHECK(std::string(NameOf(SessionLevel{})) == "SessionLevel");
  CHECK(std::string(NameOf(NpcLevel{})) == "NpcLevel");
  CHECK_FALSE(IsCombat(Digest{}));
  CHECK_FALSE(IsCombat(ShotHit{}));

  CHECK_FALSE(Decode(Encode(PlayerLevel{.level = 0})).has_value());
  CHECK_FALSE(Decode(Encode(PlayerLevel{.level = kMaxLevel + 1})).has_value());
  CHECK(Decode(Encode(PlayerLevel{.level = kMaxLevel})).has_value());
  CHECK_FALSE(Decode(Encode(SessionLevel{.level = 0, .players = 1})).has_value());
  CHECK_FALSE(Decode(Encode(SessionLevel{.level = 5, .players = 0})).has_value());
  CHECK_FALSE(Decode(Encode(NpcLevel{.npc = 1, .level = -1.f})).has_value());
  CHECK_FALSE(Decode(Encode(NpcLevel{.npc = 1, .level = 1000.f})).has_value());
  CHECK_FALSE(Decode(Encode(NpcLevel{.npc = 1, .level = std::numeric_limits<float>::quiet_NaN()}))
                  .has_value());
}

TEST_CASE("comms: pings, tags, links and waypoints survive encoding, small, reliable") {
  RoundTrips(Mark{.from = 2,
                  .id = 7,
                  .kind = MarkKind::Enemy,
                  .target = {TargetKind::Npc, 70001},
                  .position = {-1234.5f, 678.25f, 12.f},
                  .seconds = 15});
  RoundTrips(Mark{.from = 1, .id = 1, .kind = MarkKind::None});
  RoundTrips(Tag{.from = 3,
                 .reason = TagReason::Reveal,
                 .on = true,
                 .seconds = 30,
                 .targets = {{TargetKind::Npc, 12}, {TargetKind::Entity, 0xDEADBEEFCAFEull}}});
  RoundTrips(Link{.player = 2, .revision = 4, .mode = LinkMode::Everyone, .players = {3}});
  RoundTrips(Link{.player = 1, .mode = LinkMode::Listed, .players = {2, 3, 4}});
  RoundTrips(Waypoint{
      .player = 2, .revision = 9, .kind = WaypointKind::Pin, .position = {10.f, -20.f, 3.f}});
  RoundTrips(Waypoint{.player = 1,
                      .revision = 2,
                      .kind = WaypointKind::Objective,
                      .position = {-1200.f, 1850.f, 40.f}});
  for (const Message message :
       {Message{Mark{}}, Message{Tag{}}, Message{Link{}}, Message{Waypoint{}}}) {
    CHECK(ChannelOf(message) == Channel::Event);
    CHECK(IsReliable(message));
    CHECK_FALSE(IsStory(message));
    CHECK_FALSE(IsCombat(message));
  }
  CHECK(std::string(NameOf(Mark{})) == "Mark");
  CHECK(std::string(NameOf(Tag{})) == "Tag");
  CHECK(std::string(NameOf(Link{})) == "Link");
  CHECK(std::string(NameOf(Waypoint{})) == "Waypoint");
  // A ping is one small reliable packet; a squad's reveal fits in one too.
  CHECK(Encode(Mark{}).size() < 40);
  CHECK(Encode(Waypoint{}).size() < 32);
  CHECK(Encode(Waypoint{.kind = WaypointKind::Objective}).size() < 32);
  Tag squad{.targets = std::vector<TargetRef>(kMaxTagTargets, {TargetKind::Npc, 1})};
  CHECK(Encode(squad).size() < 200);

  // Kinds this version does not know, lists past their caps: refused.
  CHECK_FALSE(Decode(Encode(Mark{.kind = static_cast<MarkKind>(8)})).has_value());
  RoundTrips(Mark{
      .from = 2, .id = 9, .kind = MarkKind::Target, .target = {TargetKind::Entity, 0xC0FFEEull}});
  CHECK_FALSE(Decode(Encode(Mark{.target = {static_cast<TargetKind>(4), 1}})).has_value());
  CHECK_FALSE(Decode(Encode(Tag{.reason = static_cast<TagReason>(2)})).has_value());
  squad.targets.push_back({TargetKind::Npc, 2});
  CHECK_FALSE(Decode(Encode(squad)).has_value());
  CHECK_FALSE(Decode(Encode(Link{.mode = static_cast<LinkMode>(3)})).has_value());
  CHECK_FALSE(
      Decode(Encode(Link{.players = std::vector<PlayerId>(kMaxLinkPlayers + 1, 2)})).has_value());
  CHECK_FALSE(Decode(Encode(Waypoint{.kind = static_cast<WaypointKind>(4)})).has_value());
}

TEST_CASE("enemy scaling: NpcScale survives encoding, is small and reliable, counts checked") {
  RoundTrips(NpcScale{.npc = 0x01000007, .players = 3});
  CHECK(std::string(NameOf(NpcScale{})) == "NpcScale");
  CHECK(IsCombat(NpcScale{}));
  CHECK_FALSE(IsStory(NpcScale{}));
  CHECK(ChannelOf(NpcScale{}) == Channel::Event);
  CHECK(IsReliable(NpcScale{}));
  CHECK(Encode(NpcScale{.npc = 0xFFFFFFFF, .players = 4}).size() < 16);
  // Appended after the netcode's compact forms.
  CHECK(Message(NpcScale{}).index() > Message(VehicleMotion{}).index());
  // Nobody, or more players than a session has ids for: refused.
  CHECK_FALSE(Decode(Encode(NpcScale{.npc = 1, .players = 0})).has_value());
  CHECK_FALSE(Decode(Encode(NpcScale{.npc = 1, .players = kMaxPlayerId + 1})).has_value());
  CHECK(Decode(Encode(NpcScale{.npc = 1, .players = kMaxPlayerId})).has_value());
}

TEST_CASE("street races: Race survives encoding, is reliable, kinds and lists checked") {
  RoundTrips(Race{.player = 2,
                  .starter = 2,
                  .race = 3,
                  .kind = RaceKind::Info,
                  .arg = kRaceGrid,
                  .checkpoints = 8,
                  .heading = 92.5f,
                  .timeUs = 123456789ull,
                  .record = 0x1122334455ull,
                  .quest = 0xDEADBEEF,
                  .at = {-1200.f, 340.5f, 12.f},
                  .points = {{1.f, 2.f, 3.f}, {4.f, 5.f, 6.f}}});
  RoundTrips(Race{.player = 3, .starter = 2, .race = 3, .kind = RaceKind::Join, .arg = 1});
  RoundTrips(Race{.player = 2, .starter = 2, .to = 3, .race = 3, .kind = RaceKind::Seat, .arg = 1});
  RoundTrips(Race{.player = 3,
                  .starter = 2,
                  .race = 3,
                  .kind = RaceKind::Progress,
                  .checkpoint = 4,
                  .checkpoints = 8,
                  .metres = 120.5f,
                  .timeUs = 99});
  RoundTrips(Race{
      .player = 2,
      .starter = 2,
      .race = 3,
      .kind = RaceKind::Standings,
      .entries = {{.who = 3, .checkpoint = 8, .place = 1, .flags = kRacerFinished, .timeUs = 10},
                  {.who = kRacerNpc | 1, .checkpoint = 7, .place = 2}}});
  RoundTrips(Race{
      .player = 2, .starter = 2, .to = 3, .kind = RaceKind::Reward, .arg = 1, .record = 0x42ull});
  RoundTrips(Race{.player = 2,
                  .starter = 2,
                  .race = 3,
                  .kind = RaceKind::Route,
                  .arg = kRaceRewardsCity,
                  .checkpoints = 2,
                  .points = {{1.f, 2.f, 3.f}, {4.f, 5.f, 6.f}}});
  CHECK(std::string(NameOf(Race{})) == "Race");
  CHECK(Message(Race{}).index() == Message(TurretHands{}).index() + 1);
  CHECK(IsCombat(Race{}));
  CHECK_FALSE(IsStory(Race{}));
  CHECK(ChannelOf(Race{}) == Channel::Event);
  CHECK(Encode(Race{.kind = RaceKind::Progress}).size() < 96);
  // Kinds and phases this version does not know, lists past their caps: refused.
  CHECK_FALSE(Decode(Encode(Race{})).has_value());
  CHECK_FALSE(Decode(Encode(Race{.kind = static_cast<RaceKind>(8)})).has_value());
  CHECK_FALSE(Decode(Encode(Race{.kind = RaceKind::Route, .arg = kRaceRewardsSantoDomingo + 1}))
                  .has_value());
  CHECK_FALSE(Decode(Encode(Race{.kind = RaceKind::Info})).has_value());
  CHECK_FALSE(Decode(Encode(Race{.kind = RaceKind::Info, .arg = kRaceAbandoned + 1})).has_value());
  CHECK_FALSE(Decode(Encode(Race{.kind = RaceKind::Info,
                                 .arg = kRaceGrid,
                                 .points = std::vector<Vec3>(kMaxRacePoints + 1)}))
                  .has_value());
  CHECK_FALSE(Decode(Encode(Race{.kind = RaceKind::Standings,
                                 .entries = std::vector<RaceEntry>(kMaxRacers + 1)}))
                  .has_value());
}

TEST_CASE("what a game runs without survives encoding: reliable, small, appended, one form") {
  const Degraded some{.player = 2,
                      .entries = {{.id = "looks"},
                                  {.id = "puppet movement", .flags = kDegradedCore},
                                  {.id = "vehicle move", .flags = kDegradedChosen}}};
  RoundTrips(some);
  RoundTrips(Degraded{.player = 3});                                     // nothing off
  CHECK(Message(Degraded{}).index() == Message(HitTaken{}).index() + 1); // appended
  CHECK(std::string(NameOf(Degraded{})) == "Degraded");
  CHECK(ChannelOf(Degraded{}) == Channel::Event);
  CHECK(IsReliable(Degraded{}));
  CHECK(IsCombat(Degraded{}));
  CHECK_FALSE(IsStory(Degraded{}));
  CHECK(Encode(some).size() <= 64);
  // Refused on the wire: ids out of order or twice (a repeat must be equal),
  // an empty or long id, a character the registry never writes, a flag of
  // another version, more entries than there are states.
  const auto refused = [](const Degraded &said) { return !Decode(Encode(said)).has_value(); };
  CHECK(refused({.entries = {{.id = "vehicle move"}, {.id = "looks"}}}));
  CHECK(refused({.entries = {{.id = "looks"}, {.id = "looks"}}}));
  CHECK(refused({.entries = {{.id = ""}}}));
  CHECK(refused({.entries = {{.id = std::string(kMaxDegradedId + 1, 'a')}}}));
  CHECK_FALSE(refused({.entries = {{.id = std::string(kMaxDegradedId, 'a')}}}));
  CHECK(refused({.entries = {{.id = "Looks"}}}));
  CHECK(refused({.entries = {{.id = "looks\n"}}}));
  CHECK(refused({.entries = {{.id = "looks", .flags = 4}}}));
  Degraded many;
  for (std::size_t i = 0; i <= kMaxDegraded; ++i)
    many.entries.push_back({.id = std::string(1 + i / 26, static_cast<char>('a' + i % 26))});
  std::ranges::sort(many.entries, {}, &DegradedEntry::id);
  CHECK(refused(many));
  many.entries.pop_back();
  CHECK_FALSE(refused(many));
}

TEST_CASE("traffic lights survive encoding: reliable, appended, bounded") {
  const TrafficLights some{.owner = 2,
                           .full = true,
                           .lights = {{.entity = 0x1234'5678'9ABC'DEF0ull, .color = kLightRed},
                                      {.entity = 77, .color = kLightYellow},
                                      {.entity = 78, .color = kLightGreen}}};
  RoundTrips(some);
  RoundTrips(TrafficLights{.owner = 3});                                      // none known
  CHECK(Message(TrafficLights{}).index() == Message(Degraded{}).index() + 1); // appended
  CHECK(std::string(NameOf(TrafficLights{})) == "TrafficLights");
  CHECK(ChannelOf(TrafficLights{}) == Channel::Event);
  CHECK(IsReliable(TrafficLights{}));
  CHECK(IsCombat(TrafficLights{}));
  // Refused on the wire: a light with no entity, a colour of no version,
  // more lights than a message holds.
  const auto refused = [](const TrafficLights &said) { return !Decode(Encode(said)).has_value(); };
  CHECK(refused({.lights = {{.entity = 0}}}));
  CHECK(refused({.lights = {{.entity = 5, .color = 3}}}));
  TrafficLights many;
  many.lights.assign(kMaxLights + 1, {.entity = 5});
  CHECK(refused(many));
  many.lights.pop_back();
  CHECK_FALSE(refused(many));
  CHECK(Encode(many).size() < 1200);
}

TEST_CASE("physics props: motion on the state channel, rests and pushes reliable, a push once") {
  const PropStates states{.owner = 2,
                          .timeUs = 1'234'567,
                          .props = {{.entity = 0xB1B1'0000'0000'0001ULL,
                                     .part = 1,
                                     .position = {1.f, 2.f, 3.f},
                                     .orientation = {1, 2, 3},
                                     .velocity = {100, -50, 0}}}};
  RoundTrips(states);
  RoundTrips(PropRest{.owner = 1,
                      .entity = 0xC0C0,
                      .part = 0,
                      .position = {4.f, 5.f, 6.f},
                      .orientation = {0.f, 0.f, 0.6f, 0.8f}});
  RoundTrips(PropPush{.player = 3,
                      .entity = 0xC0C0,
                      .position = {4.f, 5.f, 6.f},
                      .velocity = {0.f, 2.f, 0.f},
                      .timeUs = 99});
  CHECK(ChannelOf(states) == Channel::State);
  CHECK(ChannelOf(PropRest{.entity = 1}) == Channel::Event);
  CHECK(ChannelOf(PropPush{.entity = 1}) == Channel::Event);
  CHECK(tandem::consistency::IsOneShot(PropPush{.entity = 1}));
  CHECK_FALSE(tandem::consistency::IsOneShot(PropRest{.entity = 1}));
  CHECK(std::string_view(NameOf(states)) == "PropStates");
  CHECK(std::string_view(NameOf(PropRest{})) == "PropRest");
  CHECK(std::string_view(NameOf(PropPush{})) == "PropPush");
  // A full round stays small: 16 props in under 600 bytes.
  PropStates full{.owner = 2, .timeUs = 1};
  for (std::uint64_t i = 1; i <= kMaxPropsPerState; ++i)
    full.props.push_back({.entity = i});
  CHECK(Encode(full).size() < 600);
  // More than a round, a prop that is no entity: refused.
  full.props.push_back({.entity = 99});
  CHECK_FALSE(Decode(Encode(full)).has_value());
  CHECK_FALSE(Decode(Encode(PropStates{.props = {{.entity = 0}}})).has_value());
  CHECK_FALSE(Decode(Encode(PropRest{})).has_value());
  CHECK_FALSE(Decode(Encode(PropPush{})).has_value());
}
