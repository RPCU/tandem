// Story sync without the game: the host's record for late joiners, "taking
// part", the first answer to a call, and the story path through real
// endpoints over loopback UDP.

#include "tandem/endpoint.hpp"
#include "tandem/story.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <functional>

#include <unistd.h>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
std::uint16_t NextPort() {
  // Apart from endpoint_test.cpp's range, and per process.
  static std::atomic<std::uint16_t> port{static_cast<std::uint16_t>(61000 + (getpid() % 400) * 10)};
  return port++;
}

bool Pump(std::vector<Endpoint *> endpoints,
          const std::function<bool(std::size_t, const std::vector<SessionEvent> &)> &done,
          std::chrono::milliseconds limit = 2s) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  bool finished = false;
  while (!finished && std::chrono::steady_clock::now() < deadline)
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      finished = done(i, endpoints[i]->Update(1ms)) || finished;
  return finished;
}

void PumpFor(std::vector<Endpoint *> endpoints, std::chrono::milliseconds time,
             const std::function<void(std::size_t, const std::vector<SessionEvent> &)> &seen) {
  Pump(
      std::move(endpoints),
      [&](std::size_t i, const auto &events) {
        seen(i, events);
        return false;
      },
      time);
}

template <class T> std::vector<T> StoryOf(const std::vector<SessionEvent> &events) {
  std::vector<T> out;
  for (const auto &event : events)
    if (const auto *story = std::get_if<events::Story>(&event))
      if (const auto *message = std::get_if<T>(&story->message))
        out.push_back(*message);
  return out;
}
} // namespace

TEST_CASE("the story record gives a joiner the journal as it is now") {
  story::Record record;
  CHECK(record.ForJoiner().empty()); // nothing from the host's game yet
  record.Note(proto::Journal{.snapshot = true, .entries = {{1, 2, 0}, {2, 3, 0}, {3, 2, 0}}});
  record.Note(proto::Journal{.entries = {{3, 1, 1}, {4, 2, 1}}}); // 3 back to Inactive, 4 new
  record.Note(proto::Tracked{4});
  record.Note(proto::PhoneCall{.contact = 9, .phase = 1});
  record.Note(proto::ChoiceHub{.hub = 5, .options = {{"Yes", 0}}});
  record.Note(proto::StoryState{.player = 2, .tier = 3});
  auto joiner = record.ForJoiner();
  REQUIRE(joiner.size() == 5);
  const auto &snapshot = std::get<proto::Journal>(joiner[0]);
  CHECK(snapshot.snapshot);
  REQUIRE(snapshot.entries.size() == 3);
  CHECK(snapshot.entries[0].hash == 1);
  CHECK(snapshot.entries[1].hash == 2);
  CHECK(snapshot.entries[2].hash == 4);
  CHECK(snapshot.entries[2].notify == 0); // no toasts for a joiner
  CHECK(std::get<proto::Tracked>(joiner[1]).hash == 4);

  // The call ends and the hub closes: nothing of them for the next joiner.
  record.Note(proto::PhoneCall{.contact = 9, .phase = 3});
  record.Note(proto::ChoiceHub{.hub = 5});
  record.Forget(2);
  joiner = record.ForJoiner();
  CHECK(joiner.size() == 2);
}

TEST_CASE("a guest takes part when near the host as the quest moves") {
  story::Participation part;
  part.Note(100, {2}, 1'000'000);
  part.Note(100, {}, 2'000'000);  // guest 3 was far
  part.Note(200, {3}, 3'000'000); // another quest, guest 3 near
  CHECK(part.Of(100) == std::set<proto::PlayerId>{2});
  CHECK(part.Of(200) == std::set<proto::PlayerId>{3});

  // A reward 5 s after quest 200 last changed is quest 200's.
  auto award = part.Attribute(8'000'000, {});
  CHECK(award.quest == 200);
  CHECK(award.players == std::vector<proto::PlayerId>{3});

  // Long after: nobody's quest; whoever is near now.
  award = part.Attribute(60'000'000, {2});
  CHECK(award.quest == 0);
  CHECK(award.players == std::vector<proto::PlayerId>{2});

  part.Forget(3);
  CHECK(part.Of(200).empty());
}

TEST_CASE("the first answer to a call wins") {
  story::CallArbiter calls;
  CHECK_FALSE(calls.TryAnswer(2, proto::kPhoneAnswer, 9)); // no call
  calls.Update({.contact = 9, .phase = 1});
  CHECK_FALSE(calls.TryAnswer(2, proto::kPhoneAnswer, 8)); // another contact
  CHECK(calls.TryAnswer(2, proto::kPhoneAnswer, 9));
  CHECK_FALSE(calls.TryAnswer(3, proto::kPhoneReject, 9)); // too late
  CHECK(calls.Call()->answeredBy == 2);
  // The call starts: who answered stays.
  calls.Update({.contact = 9, .phase = 2});
  CHECK(calls.Call()->answeredBy == 2);
  CHECK_FALSE(calls.TryAnswer(3, proto::kPhoneAnswer, 9)); // not ringing
  // A new call rings: anyone again; a reject is an answer too.
  calls.Update({.contact = 7, .phase = 1});
  CHECK(calls.TryAnswer(3, proto::kPhoneReject, 7));
  CHECK((calls.Call()->flags & proto::kCallRejected) != 0);
}

TEST_CASE("the story goes through the endpoints: journal, calls, cursors, rewards") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error);
  REQUIRE(a);
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) {
    return a->IsJoined() && host->Players().size() == 2;
  }));

  // The host's journal reaches the guest; a guest cannot speak for the story.
  host->SendStory(proto::Journal{.snapshot = true, .entries = {{11, 2, 0}, {12, 3, 0}}});
  a->SendStory(proto::Journal{.snapshot = true, .entries = {{99, 2, 0}}});
  std::vector<proto::Journal> atA, atHost;
  PumpFor({host.get(), a.get()}, 300ms, [&](std::size_t i, const auto &events) {
    for (const auto &journal : StoryOf<proto::Journal>(events))
      (i == 0 ? atHost : atA).push_back(journal);
  });
  REQUIRE(atA.size() == 1);
  CHECK(atA[0].entries.size() == 2);
  CHECK(atHost.empty());

  host->SendStory(proto::Journal{.entries = {{13, 2, 1}}});
  host->SendStory(proto::PhoneCall{.contact = 5, .phase = 1, .flags = proto::kCallRejectable});

  // A second guest joins late: it gets the journal with the change, and the call.
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error);
  REQUIRE(b);
  std::vector<proto::Journal> atB;
  std::vector<proto::PhoneCall> callsAtB;
  PumpFor({host.get(), a.get(), b.get()}, 500ms, [&](std::size_t i, const auto &events) {
    if (i == 2) {
      for (const auto &journal : StoryOf<proto::Journal>(events))
        atB.push_back(journal);
      for (const auto &call : StoryOf<proto::PhoneCall>(events))
        callsAtB.push_back(call);
    }
  });
  REQUIRE(atB.size() == 1);
  CHECK(atB[0].snapshot);
  CHECK(atB[0].entries.size() == 3);
  REQUIRE(callsAtB.size() == 1);
  CHECK(callsAtB[0].contact == 5);

  // Both guests answer: the host's game hears the first only, and everyone
  // learns who picked up.
  std::vector<proto::PhoneAction> answers;
  std::vector<proto::PhoneCall> callsAtA;
  const auto collect = [&](std::size_t i, const auto &events) {
    if (i == 0)
      for (const auto &action : StoryOf<proto::PhoneAction>(events))
        answers.push_back(action);
    if (i == 1)
      for (const auto &call : StoryOf<proto::PhoneCall>(events))
        callsAtA.push_back(call);
  };
  a->SendStory(proto::PhoneAction{.player = 77, .action = proto::kPhoneAnswer, .contact = 5});
  PumpFor({host.get(), a.get(), b.get()}, 100ms, collect);
  b->SendStory(proto::PhoneAction{.action = proto::kPhoneAnswer, .contact = 5});
  PumpFor({host.get(), a.get(), b.get()}, 300ms, collect);
  REQUIRE(answers.size() == 1);
  CHECK(answers[0].player == a->You()); // stamped by the host
  REQUIRE(callsAtA.size() == 1);
  CHECK(callsAtA[0].answeredBy == a->You());

  // A guest's cursor reaches the host and the other guest, stamped.
  b->SendStory(
      proto::ChoiceCursor{.player = 1, .hub = 3, .index = 2, .kind = proto::kCursorSuggest});
  std::vector<proto::ChoiceCursor> cursorsAtHost, cursorsAtA;
  PumpFor({host.get(), a.get(), b.get()}, 300ms, [&](std::size_t i, const auto &events) {
    for (const auto &cursor : StoryOf<proto::ChoiceCursor>(events))
      (i == 0 ? cursorsAtHost : cursorsAtA).push_back(cursor);
  });
  REQUIRE(cursorsAtHost.size() == 1);
  CHECK(cursorsAtHost[0].player == b->You());
  REQUIRE(cursorsAtA.size() == 1);
  CHECK(cursorsAtA[0].index == 2);

  // A reward goes to one guest only.
  host->SendStory(proto::QuestReward{.reward = 42, .quest = 13}, b->You());
  std::size_t rewardsA = 0, rewardsB = 0;
  PumpFor({host.get(), a.get(), b.get()}, 300ms, [&](std::size_t i, const auto &events) {
    (i == 1   ? rewardsA
     : i == 2 ? rewardsB
              : rewardsA) += i == 0 ? 0 : StoryOf<proto::QuestReward>(events).size();
  });
  CHECK(rewardsA == 0);
  CHECK(rewardsB == 1);
}
