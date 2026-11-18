#include "tandem/protocol.hpp"

#include <doctest/doctest.h>

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
