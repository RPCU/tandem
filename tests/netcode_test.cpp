// The netcode's parts (docs/architecture/netcode.md): the pose stream's
// delta coding, the NPC stream's quantized changes, the relevance and still
// gates, and the traffic counters.

#include "motion.hpp"
#include "slowdown.hpp"

#include "tandem/endpoint.hpp"
#include "tandem/lagcomp.hpp"
#include "tandem/net_traffic.hpp"
#include "tandem/netcode.hpp"
#include "tandem/pose.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

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
  std::size_t bytes = 0, frames = 0, keys = 0, nudges = 0, turns = 0;
  float worst = 0.f;
  for (std::uint32_t i = 1; i <= 300; ++i) {
    const auto pose = WirePose(static_cast<float>(i) / 30.f, i);
    const auto frame = encoder.Encode(pose);
    const auto wire = proto::Encode(frame);
    bytes += wire.size();
    ++frames;
    keys += frame.key == frame.seq;
    for (std::size_t slot = 0; slot < proto::kPoseBones; ++slot) {
      nudges += frame.Code(slot) == proto::kBoneNudge;
      turns += frame.Code(slot) == proto::kBoneTurn;
    }
    // As the wire carries it: the same frame, its time whole again.
    const auto heard = proto::Decode(wire);
    REQUIRE(heard);
    REQUIRE(std::get<proto::PoseFrame>(*heard) == frame);
    const auto back = decoder.Decode(std::get<proto::PoseFrame>(*heard));
    REQUIRE(back);
    CHECK(back->timeUs == pose.timeUs);
    CHECK(back->seq == pose.seq);
    worst = std::max(worst, WorstAngle(*back, pose));
  }
  const auto whole = proto::Encode(WirePose(1.f, 1)).size();
  MESSAGE("pose: ", bytes / frames, " bytes a frame (was ", whole, "), ", keys,
          " keys in 300 frames, ", nudges, " bones in 2 bytes, ", turns, " in 3, worst bone ",
          worst, " rad");
  CHECK(keys == 20);                                     // every 15 frames
  CHECK(worst < pose::PoseEncoder::kThreshold + 0.004f); // a turn's step
  CHECK(bytes / frames < whole / 2);
  CHECK(nudges > 0);
  CHECK(turns > 0);

  SUBCASE("a turn in two bytes is the same turn as in three") {
    // Every turn that fits 5 bits a step, as PoseEncoder writes it and as
    // PoseDecoder reads it: bit for bit what the 3-byte form gives.
    const auto key = WirePose(0.f, 1);
    pose::PoseEncoder sender;
    pose::PoseDecoder receiver;
    REQUIRE(receiver.Decode(sender.Encode(key)));
    for (int x = -proto::kPoseNudgeSteps; x < proto::kPoseNudgeSteps; x += 5)
      for (int y = -proto::kPoseNudgeSteps; y < proto::kPoseNudgeSteps; y += 3)
        for (int z = -proto::kPoseNudgeSteps; z < proto::kPoseNudgeSteps; ++z) {
          proto::PoseFrame frame{.player = 2, .seq = 2, .timeUs = 7, .key = 1};
          auto longer = frame;
          const std::int8_t turn[3] = {static_cast<std::int8_t>(x), static_cast<std::int8_t>(y),
                                       static_cast<std::int8_t>(z)};
          const auto bits = static_cast<std::uint16_t>((x & 31) | (y & 31) << 5 | (z & 31) << 10);
          frame.codes[0] = proto::kBoneNudge;
          frame.rotations = {static_cast<std::uint8_t>(bits), static_cast<std::uint8_t>(bits >> 8)};
          longer.codes[0] = proto::kBoneTurn;
          for (const auto step : turn)
            longer.rotations.push_back(static_cast<std::uint8_t>(step));
          const auto two = receiver.Decode(frame), three = receiver.Decode(longer);
          REQUIRE(two);
          REQUIRE(three);
          REQUIRE(two->rotations == three->rotations);
        }
  }
  SUBCASE("a frame's short time is made whole across the 32-bit turn and out of order") {
    pose::PoseEncoder sender;
    pose::PoseDecoder receiver;
    const std::uint64_t turnUs = 7ull << 32;
    std::vector<std::pair<proto::PoseFrame, std::uint64_t>> sent;
    for (std::uint32_t i = 1; i <= 45; ++i) {
      auto pose = WirePose(static_cast<float>(i) / 30.f, i);
      pose.timeUs = turnUs - 20 * kFrame + i * kFrame; // the 20th is at the turn
      sent.emplace_back(sender.Encode(pose), pose.timeUs);
    }
    CHECK(sent[19].first.timeUs < proto::kPoseTimeMask); // short: not a key
    for (std::size_t i = 0; i < sent.size(); ++i) {
      // The 17th comes after the next key (the 31st).
      const auto at = i >= 16 && i < 30 ? i + 1 : i == 30 ? 16 : i;
      const auto back = receiver.Decode(sent[at].first);
      REQUIRE(back);
      CHECK(back->timeUs == sent[at].second);
    }
    // Its key lost, from a key past the turn: back across it.
    pose::PoseDecoder late;
    REQUIRE(late.Decode(sent[30].first));
    const auto back = late.Decode(sent[17].first);
    REQUIRE(back);
    CHECK(back->timeUs == sent[17].second);
    CHECK(back->timeUs < turnUs);
  }

  SUBCASE("a lost key: the next frames use the newest key there is") {
    pose::PoseEncoder sender;
    pose::PoseDecoder receiver;
    float error = 0.f;
    for (std::uint32_t i = 1; i <= 60; ++i) {
      const auto pose = WirePose(static_cast<float>(i) / 30.f, i, 0.2f);
      const auto frame = sender.Encode(pose);
      if (i == 16)
        continue; // the second key, lost
      if (const auto back = receiver.Decode(frame)) {
        error = std::max(error, WorstAngle(*back, pose));
        CHECK(back->timeUs == pose.timeUs); // from the older key's time
      }
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

TEST_CASE("NPC states go as their changes, quantized, with their pace") {
  netcode::NpcCoder coder;
  netcode::NpcVelocities velocities;
  std::map<proto::NpcId, proto::NpcEntry> book; // what the receiver last read of each
  std::size_t sent = 0;
  float worst = 0.f, worstSpeed = 0.f;
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
    velocities.Fill(states);
    const auto motion = coder.Encode(states);
    REQUIRE(motion);
    if (seq > 1)
      sent += motion->npcs.size();
    for (const auto &entry : netcode::Read(*motion).npcs)
      book[entry.npc] = entry;
    REQUIRE(book.size() == 20);
    for (const auto &b : states.npcs) {
      const auto &a = book.at(b.npc);
      CHECK(a.detected == b.detected); // a change of what it sees always goes at once
      worst = std::max(worst, std::hypot(a.position.x - b.position.x, a.position.y - b.position.y,
                                         a.position.z - b.position.z));
      if (seq > 1)
        worstSpeed = std::max(worstSpeed, std::abs(a.velocity.x - (b.npc % 2 ? 1.5f : 0.f)));
    }
  }
  MESSAGE("npcs: ", sent / 49.0, " of 20 sent a frame, worst ", worst, " m, ", worstSpeed, " m/s");
  // The 10 walking every frame; the standing ones once in kRefreshFrames.
  CHECK(sent / 49.0 < 10 + 10.0 / netcode::NpcCoder::kRefreshFrames + 1);
  CHECK(worst <= netcode::NpcCoder::kMoveM + netcode::NpcCoder::kStep);
  CHECK(worstSpeed <= 0.01f); // the owner's own pace, to the centimetre a second

  SUBCASE("one that stops is said at once, not carried on") {
    proto::NpcStates states{.seq = 51, .timeUs = test::kEpoch + 5'100'000};
    states.npcs.push_back({.npc = 1, .position = {-899.f + 7.5f, 1880.f, 36.2f}, .yaw = -2.7f});
    velocities.Fill(states);
    const auto motion = coder.Encode(states, true);
    REQUIRE(motion);
    REQUIRE(motion->npcs.size() == 1);
    CHECK(netcode::Read(*motion).npcs[0].velocity == proto::Vec3{});
  }
  SUBCASE("a partial list leaves the others as they were; a whole one forgets them") {
    proto::NpcStates near{.seq = 51, .timeUs = test::kEpoch + 5'100'000};
    near.npcs.push_back({.npc = 1, .position = {-899.f + 7.65f, 1880.f, 36.2f}});
    REQUIRE(coder.Encode(near, true));
    // Npc 2 was not in it: still known, so not sent again while it stands.
    proto::NpcStates whole{.seq = 52, .timeUs = test::kEpoch + 5'200'000};
    whole.npcs.push_back(
        {.npc = 2, .position = {-898.f, 1880.f, 36.2f}, .yaw = 0.3f * 2 - 3.f}); // as it stood
    const auto motion = coder.Encode(whole);
    REQUIRE(motion);
    CHECK(motion->npcs.size() <= 1); // a refresh at most
    // Npc 1 was not in the whole list: forgotten, sent whole when it is back.
    proto::NpcStates back{.seq = 53, .timeUs = test::kEpoch + 5'300'000};
    back.npcs.push_back({.npc = 1, .position = {-899.f + 7.65f, 1880.f, 36.2f}});
    const auto again = coder.Encode(back);
    REQUIRE(again);
    CHECK(again->npcs.size() == 1);
  }
  SUBCASE("too far apart to quantize: sent whole") {
    proto::NpcStates states{.seq = 51};
    states.npcs.push_back({.npc = 1, .position = {0.f, 0.f, 0.f}});
    states.npcs.push_back({.npc = 2, .position = {600.f, 0.f, 0.f}});
    CHECK_FALSE(coder.Encode(states));
  }
}

TEST_CASE("a dense crowd goes whole, a datagram a message") {
  proto::NpcStates crowd{.seq = 1, .timeUs = test::kEpoch};
  for (proto::NpcId npc = 1; npc <= proto::kMaxNpcsShared; ++npc)
    crowd.npcs.push_back(
        {.npc = npc,
         .position = {static_cast<float>(npc % 13), static_cast<float>(npc / 13), 0},
         .velocity = {1.f, 0.f, 0.f}});
  netcode::NpcCoder coder;
  const auto motion = coder.Encode(crowd);
  REQUIRE(motion);
  CHECK(motion->npcs.size() == proto::kMaxNpcsShared);
  const auto parts = netcode::Split(*motion);
  CHECK(parts.size() ==
        (proto::kMaxNpcsShared + netcode::kNpcsPerMotion - 1) / netcode::kNpcsPerMotion);
  std::set<proto::NpcId> seen;
  for (const auto &part : parts) {
    CHECK(part.npcs.size() <= netcode::kNpcsPerMotion);
    CHECK(proto::Encode(part).size() < 1200);
    for (const auto &entry : netcode::Read(part).npcs)
      seen.insert(entry.npc);
  }
  CHECK(seen.size() == proto::kMaxNpcsShared);
  for (const auto &part : netcode::Split(crowd)) {
    CHECK(part.npcs.size() <= netcode::kNpcsPerStates);
    CHECK(proto::Encode(part).size() < 1200);
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

  SUBCASE("a still state held over a pause in the offers is not sent") {
    netcode::StillGate<proto::PlayerState> paused;
    std::uint64_t t = test::kEpoch;
    for (int i = 0; i < 10; ++i, t += kFrame)
      paused.Offer(proto::PlayerState{.timeUs = t}, 0);
    // Nothing offered for 4 s (our V elsewhere), then it moves.
    t += 4'000'000;
    const auto out = paused.Offer(proto::PlayerState{.timeUs = t, .position = {1.f, 0.f, 0.f}}, 0);
    REQUIRE(out.size() == 1);
    CHECK(out[0].timeUs == t);
  }

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

TEST_CASE("a V's and a car's state carry the one sent before them, read back within a mm") {
  const proto::PlayerState was{.player = 2,
                               .seq = 7,
                               .timeUs = test::kEpoch,
                               .position = {-900.f, 1880.f, 36.f},
                               .orientation = sync::FromYaw(0.5f),
                               .velocity = {1.5f, 0.f, 0.f},
                               .stance = 1};
  auto now = was;
  now.seq = 8;
  now.timeUs = test::kEpoch + 33'333;
  now.position = {-899.95f, 1880.f, 36.f};
  now.orientation = sync::FromYaw(0.6f);
  const auto motion = netcode::Compact(now, &was);
  const auto decoded = proto::Decode(proto::Encode(motion));
  REQUIRE(decoded);
  const auto *wire = std::get_if<proto::PlayerMotion>(&*decoded);
  REQUIRE(wire);
  const auto back = netcode::Before(*wire);
  REQUIRE(back);
  CHECK(back->seq == 7);
  CHECK(std::abs(static_cast<std::int64_t>(back->timeUs - was.timeUs)) <= 100);
  CHECK(std::hypot(back->position.x - was.position.x, back->position.y - was.position.y) < 0.001f);
  CHECK(std::abs(sync::YawOf(back->orientation) - 0.5f) < 0.001f);
  CHECK(std::abs(back->velocity.x - 1.5f) < 0.01f);
  CHECK(back->stance == 1);
  // None for the first, a pause or a teleport.
  CHECK_FALSE(netcode::Before(netcode::Compact(now, nullptr)));
  auto far = was;
  far.position.x -= 100.f;
  CHECK_FALSE(netcode::Before(netcode::Compact(now, &far)));
  auto old = was;
  old.timeUs -= 10'000'000;
  CHECK_FALSE(netcode::Before(netcode::Compact(now, &old)));

  const proto::VehicleState car{.player = 2,
                                .seq = 3,
                                .timeUs = test::kEpoch,
                                .position = {10.f, 0.f, 0.f},
                                .orientation = sync::FromYaw(1.f),
                                .velocity = {20.f, 0.f, 0.f}};
  auto next = car;
  next.seq = 4;
  next.timeUs += 33'333;
  next.position.x += 0.66f;
  const auto carBack = netcode::Before(netcode::Compact(next, &car));
  REQUIRE(carBack);
  CHECK(carBack->seq == 3);
  CHECK(std::abs(carBack->position.x - 10.f) < 0.001f);
  CHECK(std::abs(sync::YawOf(carBack->orientation) - 1.f) < 0.002f);
}

TEST_CASE("relevance: poses and states by the distance to the nearest V") {
  CHECK(netcode::PoseIntervalUs(netcode::kNearest) == 0);
  CHECK(netcode::PoseIntervalUs(10.f) == 0);
  CHECK(netcode::PoseIntervalUs(50.f) == 0);
  CHECK(netcode::PoseIntervalUs(150.f) == 66'667);
  CHECK(netcode::PoseIntervalUs(300.f) == 200'000);
  CHECK(netcode::PoseIntervalUs(1000.f) == 1'000'000);
  CHECK(netcode::StateIntervalUs(50.f) == 0);
  CHECK(netcode::StateIntervalUs(1000.f) == 200'000);
  netcode::Gate gate;
  int due = 0;
  for (std::uint64_t t = 0; t < 1'000'000; t += kFrame) // 31 frames: 0 to 0.99999 s
    due += gate.Due(test::kEpoch + t, netcode::PoseIntervalUs(150.f));
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
  REQUIRE(
      pump(test::Patience(5s), [&] { return guest->IsJoined() && host->Players().size() == 2; }));
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
  // Each but the first with the one before it (14 bytes: netcode.md, Loss).
  CHECK(delta.kinds[state][net::kSent].bytes ==
        30 * proto::Encode(proto::PlayerMotion{}).size() + 29 * 14);
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
