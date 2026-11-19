// The netcode's parts (docs/architecture/netcode.md): the pose stream's
// delta coding, the NPC stream's quantized changes, the relevance and still
// gates, and the traffic counters.

#include "motion.hpp"

#include "tandem/endpoint.hpp"
#include "tandem/lagcomp.hpp"
#include "tandem/netcode.hpp"
#include "tandem/pose.hpp"
#include "tandem/traffic.hpp"

#include <doctest/doctest.h>

#include <functional>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
constexpr std::uint64_t kFrame = 33'333;

proto::PoseState WirePose(float s, std::uint32_t seq, float energy = 1.f) {
  auto state = pose::Pack(test::WalkCycle(s, energy));
  state.player = 2;
  state.seq = seq;
  state.timeUs = test::kEpoch + static_cast<std::uint64_t>(s * 1e6f);
  return state;
}

float WorstAngle(const proto::PoseState &a, const proto::PoseState &b) {
  float worst = 0.f;
  for (std::size_t slot = 0; slot < proto::kPoseBones; ++slot)
    worst = std::max(worst, pose::Angle(pose::UnpackQuat(a.rotations.data() + slot * 6),
                                        pose::UnpackQuat(b.rotations.data() + slot * 6)));
  return worst;
}
} // namespace

TEST_CASE("a 5-byte quaternion is within 0.0015 rad") {
  float worst = 0.f;
  for (int i = 0; i < 2000; ++i) {
    const float a = static_cast<float>(i) * 0.0131f, b = static_cast<float>(i) * 0.0077f;
    const auto q =
        pose::Normalized({std::sin(a), std::cos(b) * 0.3f, std::sin(a + b), std::cos(a)});
    std::uint8_t bytes[proto::kPoseFrameRotationBytes];
    pose::PackQuat5(q, bytes);
    worst = std::max(worst, pose::Angle(q, pose::UnpackQuat5(bytes)));
  }
  MESSAGE("5-byte quaternion: worst ", worst, " rad");
  CHECK(worst < 0.0015f);
}

TEST_CASE("a pose stream is delta coded: keys whole, frames with the bones that moved") {
  pose::PoseEncoder encoder;
  pose::PoseDecoder decoder;
  std::size_t bytes = 0, frames = 0, keys = 0;
  float worst = 0.f;
  for (std::uint32_t i = 1; i <= 300; ++i) {
    const auto pose = WirePose(static_cast<float>(i) / 30.f, i);
    const auto frame = encoder.Encode(pose);
    bytes += proto::Encode(frame).size();
    ++frames;
    keys += frame.key == frame.seq;
    const auto back = decoder.Decode(frame);
    REQUIRE(back);
    worst = std::max(worst, WorstAngle(*back, pose));
  }
  const auto whole = proto::Encode(WirePose(1.f, 1)).size();
  MESSAGE("pose: ", bytes / frames, " bytes a frame (was ", whole, "), ", keys,
          " keys in 300 frames, worst bone ", worst, " rad");
  CHECK(keys == 20);                                     // every 15 frames
  CHECK(worst < pose::PoseEncoder::kThreshold + 0.004f); // a turn's step
  CHECK(bytes / frames < whole / 2);

  SUBCASE("a lost key: the next frames use the newest key there is") {
    pose::PoseEncoder sender;
    pose::PoseDecoder receiver;
    float error = 0.f;
    for (std::uint32_t i = 1; i <= 60; ++i) {
      const auto pose = WirePose(static_cast<float>(i) / 30.f, i, 0.2f);
      const auto frame = sender.Encode(pose);
      if (i == 16)
        continue; // the second key, lost
      if (const auto back = receiver.Decode(frame))
        error = std::max(error, WorstAngle(*back, pose));
    }
    CHECK(receiver.OtherKey() == 14);
    CHECK(receiver.Dropped() == 0);
    MESSAGE("over the older key: worst bone ", error, " rad");
    CHECK(error < 0.2f);
  }
  SUBCASE("no key yet: dropped") {
    pose::PoseEncoder sender;
    pose::PoseDecoder receiver;
    sender.Encode(WirePose(0.f, 1));
    CHECK_FALSE(receiver.Decode(sender.Encode(WirePose(0.033f, 2))));
    CHECK(receiver.Dropped() == 1);
  }
}

TEST_CASE("NPC states go as their changes, quantized, and come back whole") {
  netcode::NpcCoder coder;
  netcode::NpcBook book;
  std::map<proto::NpcId, proto::NpcSpawn> known;
  for (proto::NpcId npc = 1; npc <= 20; ++npc)
    known[npc] = {.npc = npc};
  std::size_t sent = 0;
  float worst = 0.f;
  for (std::uint32_t seq = 1; seq <= 50; ++seq) {
    proto::NpcStates states{.seq = seq, .timeUs = test::kEpoch + seq * 100'000ull};
    for (proto::NpcId npc = 1; npc <= 20; ++npc) {
      // Half walk at 1.5 m/s, half stand.
      const float moved = npc % 2 ? 0.15f * static_cast<float>(seq) : 0.f;
      states.npcs.push_back({.npc = npc,
                             .position = {-900.f + static_cast<float>(npc) + moved, 1880.f, 36.2f},
                             .yaw = 0.3f * static_cast<float>(npc) - 3.f,
                             .detected = static_cast<std::uint8_t>(seq > 40 && npc == 4 ? 2 : 0)});
    }
    const auto motion = coder.Encode(states);
    REQUIRE(motion);
    if (seq > 1)
      sent += motion->npcs.size();
    const auto whole = book.Apply(*motion, known);
    REQUIRE(whole.npcs.size() == 20);
    for (std::size_t i = 0; i < 20; ++i) {
      const auto &a = whole.npcs[i], &b = states.npcs[i];
      CHECK(a.npc == b.npc);
      CHECK(a.detected == b.detected); // a change of what it sees always goes at once
      worst = std::max(worst, std::hypot(a.position.x - b.position.x, a.position.y - b.position.y,
                                         a.position.z - b.position.z));
    }
  }
  MESSAGE("npcs: ", sent / 49.0, " of 20 sent a frame, worst ", worst, " m");
  // The 10 walking every frame; the standing ones once in kRefreshFrames.
  CHECK(sent / 49.0 < 10 + 10.0 / netcode::NpcCoder::kRefreshFrames + 1);
  CHECK(worst <= netcode::NpcCoder::kMoveM + netcode::NpcCoder::kStep);

  SUBCASE("an NPC left out of the list is said once, and drops out") {
    proto::NpcStates states{.seq = 51, .timeUs = test::kEpoch + 5'100'000};
    states.npcs.push_back({.npc = 1, .position = {-899.f + 7.65f, 1880.f, 36.2f}});
    const auto motion = coder.Encode(states);
    REQUIRE(motion);
    CHECK(motion->npcs.size() == 20); // 19 unlisted + npc 1 (moved)
    CHECK(book.Apply(*motion, known).npcs.size() == 1);
  }
  SUBCASE("too far apart to quantize: sent whole") {
    proto::NpcStates states{.seq = 51};
    states.npcs.push_back({.npc = 1, .position = {0.f, 0.f, 0.f}});
    states.npcs.push_back({.npc = 2, .position = {600.f, 0.f, 0.f}});
    CHECK_FALSE(coder.Encode(states));
  }
}

TEST_CASE("a still V goes 5 times a second, and leaves from where it stood") {
  netcode::StillGate<proto::PlayerState> gate;
  std::vector<proto::PlayerState> sent;
  for (std::uint32_t i = 0; i < 90; ++i) {
    proto::PlayerState state{.timeUs = test::kEpoch + i * kFrame};
    // Still for 2 s, then walks off.
    if (i >= 60)
      state.position.x = 0.05f * static_cast<float>(i - 59);
    if (i >= 60)
      state.velocity.x = 1.5f;
    for (const auto &out : gate.Offer(state, 0))
      sent.push_back(out);
  }
  // 60 still frames: 1 + one per 200 ms; then the held one and every frame.
  std::size_t still = 0;
  for (const auto &s : sent)
    still += s.position.x == 0.f;
  CHECK(still >= 10);
  CHECK(still <= 13);
  // The frame before it moved went out, with its own time.
  const auto first = std::ranges::find_if(sent, [](const auto &s) { return s.position.x > 0.f; });
  REQUIRE(first != sent.begin());
  CHECK((first - 1)->timeUs == test::kEpoch + 59 * kFrame);
  CHECK(sent.end() - first == 30);

  SUBCASE("far away: a few states a second") {
    netcode::StillGate<proto::PlayerState> far;
    std::size_t n = 0;
    for (std::uint32_t i = 0; i < 90; ++i) {
      const proto::PlayerState state{.timeUs = test::kEpoch + i * kFrame,
                                     .position = {static_cast<float>(i), 0.f, 0.f},
                                     .velocity = {30.f, 0.f, 0.f}};
      n += far.Offer(state, netcode::StateIntervalUs(500.f)).size();
    }
    CHECK(n == 15); // 3 s at 5 Hz
  }
}

TEST_CASE("relevance: poses and states by the distance to the nearest V") {
  CHECK(netcode::PoseIntervalUs(netcode::kNearest) == 0);
  CHECK(netcode::PoseIntervalUs(10.f) == 0);
  CHECK(netcode::PoseIntervalUs(50.f) > 0);
  CHECK(netcode::PoseIntervalUs(1000.f) == 1'000'000);
  CHECK(netcode::StateIntervalUs(50.f) == 0);
  CHECK(netcode::StateIntervalUs(1000.f) == 200'000);
  netcode::Gate gate;
  int due = 0;
  for (std::uint64_t t = 0; t < 1'000'000; t += kFrame) // 31 frames: 0 to 0.99999 s
    due += gate.Due(test::kEpoch + t, netcode::PoseIntervalUs(50.f));
  CHECK(due == 16);
}

TEST_CASE("a session counts its traffic by message, channel and direction") {
  std::string error;
  auto host = Endpoint::Host({"host"}, 0, error);
  REQUIRE_MESSAGE(host, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", host->LocalPort(), error);
  REQUIRE_MESSAGE(guest, error);
  const auto pump = [&](std::chrono::milliseconds time, const std::function<bool()> &done) {
    const auto deadline = std::chrono::steady_clock::now() + time;
    while (std::chrono::steady_clock::now() < deadline && !done()) {
      host->Update(1ms);
      guest->Update(1ms);
    }
    return done();
  };
  REQUIRE(pump(5s, [&] { return guest->IsJoined() && host->Players().size() == 2; }));
  const auto before = guest->Traffic();
  for (int i = 0; i < 30; ++i) {
    guest->SendLocalState({.position = {static_cast<float>(i), 0.f, 0.f}});
    guest->SetLocalHeld({.right = static_cast<std::uint64_t>(i + 1)});
    pump(5ms, [] { return false; });
  }
  pump(300ms, [] { return false; });
  const auto delta = guest->Traffic() - before;
  // A V's state goes compact (PlayerMotion, netcode.md).
  const auto state = proto::Message(proto::PlayerMotion{}).index();
  const auto held = proto::Message(proto::Held{}).index();
  CHECK(delta.kinds[state][net::kSent].messages == 30);
  CHECK(delta.kinds[held][net::kSent].messages == 30);
  CHECK(delta.kinds[state][net::kSent].bytes == 30 * proto::Encode(proto::PlayerMotion{}).size());
  CHECK(delta.channels[0][net::kSent].messages >= 30);
  CHECK(delta.channels[1][net::kSent].messages >= 30);
  // ENet's own: datagrams both ways, acknowledgements included.
  CHECK(delta.wire[net::kSent].messages > 0);
  CHECK(delta.wire[net::kReceived].messages > 0);
  CHECK(delta.wire[net::kSent].bytes > delta.channels[0][net::kSent].bytes);
  const auto hostSide = host->Traffic();
  CHECK(hostSide.kinds[state][net::kReceived].messages >= 30);
  const auto table = net::Table(delta, 0.5);
  CHECK(table.find("PlayerMotion") != std::string::npos);
  CHECK(net::Summary(delta, 0.5).starts_with("up "));
  MESSAGE(table);
}

TEST_CASE("a guest's grab is judged where it saw the NPC, a bounded rewind back") {
  // The guest's V stands at the origin; the host's NPC walks away along +X
  // at 3 m/s and is 6.6 m off when the request lands (a 6 m reach).
  const std::uint64_t arrival = test::kEpoch + 5'000'000;
  lagcomp::History guest, npc;
  for (std::uint64_t t = test::kEpoch; t <= arrival; t += 33'333)
    guest.Push(t, {0.f, 0.f, 0.f});
  const auto npcAt = [&](std::uint64_t t) {
    return proto::Vec3{6.6f - 3.f * static_cast<float>(arrival - t) / 1e6f, 0.f, 0.f};
  };
  for (std::uint64_t t = test::kEpoch; t <= arrival; t += 100'000)
    npc.Push(t, npcAt(t));
  const proto::Vec3 puppet{-0.3f, 0.f, 0.f}; // our puppet of it, drawn a little behind

  // Without a rewind it is refused, as "too far" was in game.
  CHECK_FALSE(lagcomp::Check(nullptr, nullptr, puppet, npcAt(arrival), arrival, 150, 6.f).ok);
  // At 150 ms the guest saw it 265 ms ago: 5.8 m away. Taken.
  const auto fair = lagcomp::Check(&guest, &npc, puppet, npcAt(arrival), arrival, 150, 6.f);
  MESSAGE("now ", fair.now, " m, as seen ", fair.seen, " m");
  CHECK(fair.ok);
  CHECK(fair.seen < fair.now);
  // A link of a second buys 400 ms, no more: an NPC 9.6 m off now was 8.4 m
  // off then, and is refused.
  CHECK(lagcomp::SeenAtUs(arrival, 1000, 115'000) == arrival - lagcomp::kMaxRewindUs);
  const proto::Vec3 farNow{9.6f, 0.f, 0.f};
  lagcomp::History away;
  for (std::uint64_t t = test::kEpoch; t <= arrival; t += 100'000)
    away.Push(t, {9.6f - 3.f * static_cast<float>(arrival - t) / 1e6f, 0.f, 0.f});
  CHECK_FALSE(lagcomp::Check(&guest, &away, puppet, farNow, arrival, 1000, 6.f).ok);
  // The book keeps one history per id.
  lagcomp::Book book;
  book.Push(7, test::kEpoch, {1.f, 2.f, 3.f});
  REQUIRE(book.Of(7));
  CHECK(book.Of(7)->At(test::kEpoch + 1)->x == 1.f);
  CHECK_FALSE(book.Of(8));
}
