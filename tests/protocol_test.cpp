#include "tandem/protocol.hpp"

#include <doctest/doctest.h>

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
      NpcStates{.seq = 2, .timeUs = 9, .npcs = {{.npc = 7, .position = {1, 2, 3}, .yaw = 1.f}}});
  RoundTrips(NpcGone{.npc = 7, .reason = 1, .killer = 2});
  RoundTrips(NpcHit{.player = 2,
                    .npc = 7,
                    .damage = 41.5f,
                    .flags = kHitWeakSpot | kHitLethal,
                    .position = {1, 2, 3},
                    .direction = {0, 1, 0}});
  RoundTrips(NpcEffect{.player = 2, .npc = 7, .effects = {11, 12}});
  RoundTrips(NpcHealth{.npc = 7, .health = 0.25f});
  RoundTrips(World{
      .revision = 2, .gameTimeSeconds = 86400 + 3600 * 18, .weather = 0x1234, .timePaused = true});
  RoundTrips(Pause{.player = 3, .paused = true});
  RoundTrips(Pause{.player = 2, .paused = false});
  RoundTrips(TimeEffect{.player = 2, .abilities = kTimeSandevistan, .scale = 0.25f});
  RoundTrips(TimeEffect{.player = 3, .abilities = kTimeScanning | kTimeSlowMo, .scale = 1.f});
  CHECK(std::string(NameOf(TimeEffect{})) == "TimeEffect");
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
  CHECK_FALSE(Decode(Encode(NpcCue{.cue = NpcCueKind{3}})).has_value());
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
  crowd.npcs.resize(kMaxNpcsPerState);
  CHECK(Encode(crowd).size() < 1400); // one datagram
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
