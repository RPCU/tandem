// What a game runs without (core/include/tandem/degraded.hpp,
// docs/architecture/degradations.md): the catalogue, the registry's states
// and fallbacks, what it tells the session and the tools.

#include "tandem/degraded.hpp"

#include <doctest/doctest.h>

#include <set>
#include <string>
#include <vector>

using namespace tandem;
using degraded::Class;
using degraded::Kind;
using degraded::Registry;

TEST_CASE("degraded: every id of the catalogue is one the wire takes, once, and says what it "
          "means") {
  std::set<std::string> seen;
  for (const auto &info : degraded::Catalogue()) {
    CAPTURE(info.id);
    CHECK(seen.insert(std::string(info.id)).second);
    CHECK(degraded::WireId(info.id) == info.id);
    CHECK(proto::Fits(proto::Degraded{.entries = {{.id = std::string(info.id)}}}));
    CHECK_FALSE(info.means.empty());
    CHECK(degraded::Find(info.id) == &info);
    // A fallback is taken for one body: never what co-op cannot run without.
    if (info.kind == Kind::Fallback)
      CHECK(info.klass == Class::Degraded);
  }
  CHECK(degraded::Find("no such thing") == nullptr);
  // The core ones (degradations.md argues each): a change here is a decision.
  std::vector<std::string> core;
  for (const auto &info : degraded::Catalogue())
    if (info.klass == Class::Core)
      core.emplace_back(info.id);
  CHECK(core ==
        std::vector<std::string>{"puppet movement", "pose stream", "quest nodes", "quest freeze"});
  // Every state at once fits one message.
  std::size_t states = 0;
  for (const auto &info : degraded::Catalogue())
    states += info.kind == Kind::State ? 1 : 0;
  CHECK(states <= proto::kMaxDegraded);
}

TEST_CASE("degraded: a state holds from its start to its end, with its first reason") {
  Registry registry;
  CHECK(registry.Summary() == "degraded: none");
  CHECK(registry.List().empty());
  const auto before = registry.Changes();
  CHECK(registry.Start("looks", "hash 386815609 is not in this game's address library"));
  CHECK_FALSE(registry.Start("looks", "another reason")); // it holds already
  CHECK(registry.Changes() == before + 1);
  REQUIRE(registry.List().size() == 1);
  const auto entry = registry.List().front();
  CHECK(entry.id == "looks");
  CHECK(entry.klass == Class::Degraded);
  CHECK(entry.kind == Kind::State);
  CHECK(entry.known);
  CHECK_FALSE(entry.chosen);
  CHECK(entry.reason == "hash 386815609 is not in this game's address library");
  CHECK(registry.Summary() == "degraded: looks");
  CHECK(registry.CoreOff().empty());
  CHECK(registry.End("looks"));
  CHECK_FALSE(registry.End("looks"));
  CHECK(registry.Summary() == "degraded: none");
  CHECK(registry.Changes() == before + 2);
}

TEST_CASE("degraded: a core state means no session, unless the player's own switch chose it") {
  Registry registry;
  // tandem.ini pose = off: listed, no failure, co-op runs.
  CHECK(registry.Start("pose stream", "tandem.ini pose = off", true));
  CHECK(registry.CoreOff().empty());
  CHECK(registry.Summary() == "degraded: pose stream (core, chosen)");
  // A switch never hides what is really off: the reason nobody chose wins,
  // and stays.
  CHECK(registry.Start("pose stream", "animRig is 0x188 bytes, measured as 0x180 on 2.50"));
  CHECK_FALSE(registry.Start("pose stream", "tandem.ini pose = off", true));
  REQUIRE(registry.CoreOff().size() == 1);
  CHECK(registry.CoreOff().front().reason == "animRig is 0x188 bytes, measured as 0x180 on 2.50");
  CHECK(registry.Start("puppet movement", "class moveComponent not found"));
  CHECK(registry.Start("vehicle move", "tandem.ini kinematic_ride = off", true));
  // The catalogue's order, whatever the order they started in.
  CHECK(registry.Summary() ==
        "degraded: puppet movement (core), pose stream (core), vehicle move (chosen)");
  CHECK(registry.CoreOff().size() == 2);
  const auto problem = degraded::CoreProblem(registry.CoreOff().front());
  CHECK(problem.starts_with(degraded::kCoreProblem));
  CHECK(problem.find("puppet movement is off (class moveComponent not found)") !=
        std::string::npos);
  CHECK(problem.ends_with("Co-op is off."));
}

TEST_CASE("degraded: a fallback is counted with its last reason, and forgotten at a test's start") {
  Registry registry;
  CHECK(registry.Count("prompt drawn", "a: its body has no Interaction component") == 1);
  CHECK(registry.Count("prompt drawn", "b: no prompt for 3 s") == 2);
  CHECK(registry.Count("vehicle teleported", "body 7 does not follow") == 1);
  CHECK(registry.Start("looks", "why"));
  // States first, then fallbacks, each in the catalogue's order.
  const auto list = registry.List();
  REQUIRE(list.size() == 3);
  CHECK(list[0].id == "looks");
  CHECK(list[1].id == "prompt drawn");
  CHECK(list[1].kind == Kind::Fallback);
  CHECK(list[1].count == 2);
  CHECK(list[1].reason == "b: no prompt for 3 s");
  CHECK(list[2].id == "vehicle teleported");
  CHECK(registry.Summary() == "degraded: looks; fallbacks: prompt drawn x2, vehicle teleported x1");
  // What the tools read (tools/game.sh degraded).
  CHECK(registry.Report() ==
        std::vector<std::string>{
            "degraded: registry: 0 core, 1 degraded, 0 chosen, 2 fallbacks (3 times)",
            "degraded: state looks [degraded]: why",
            "degraded: fallback prompt drawn [degraded] x2: b: no prompt for 3 s",
            "degraded: fallback vehicle teleported [degraded] x1: body 7 does not follow",
            "degraded: registry end"});
  // The session is told the states only.
  CHECK(registry.Wire() == proto::Degraded{.entries = {{.id = "looks"}}});
  registry.ResetFallbacks();
  CHECK(registry.Summary() == "degraded: looks");
  CHECK(Registry().Report() ==
        std::vector<std::string>{
            "degraded: registry: 0 core, 0 degraded, 0 chosen, 0 fallbacks (0 times)",
            "degraded: registry end"});
}

TEST_CASE("degraded: an id the catalogue lacks is still listed, as unknown") {
  Registry registry;
  CHECK(registry.Start("Some New Path!", "why"));
  CHECK(registry.Count("another one", "why") == 1);
  // A state said as a fallback (or the other way round) is unknown too.
  CHECK(registry.Count("looks", "why") == 1);
  const auto list = registry.List();
  REQUIRE(list.size() == 3);
  for (const auto &entry : list) {
    CHECK_FALSE(entry.known);
    CHECK(entry.klass == Class::Degraded);
    CHECK(degraded::LineOf(entry).find("unknown id") != std::string::npos);
  }
  // Told all the same, in a form the wire takes.
  const auto wire = registry.Wire();
  REQUIRE(wire.entries.size() == 1);
  CHECK(wire.entries.front().id == "some new path-");
  CHECK(proto::Fits(wire));
}

TEST_CASE("degraded: the session is told the states that hold, sorted, with their flags") {
  Registry registry;
  CHECK(registry.Wire() == proto::Degraded{});
  registry.Start("vehicle move", "tandem.ini kinematic_ride = off", true);
  registry.Start("puppet movement", "why");
  registry.Start("looks", "why");
  const auto wire = registry.Wire();
  CHECK(proto::Fits(wire));
  CHECK(wire ==
        proto::Degraded{.entries = {{.id = "looks"},
                                    {.id = "puppet movement", .flags = proto::kDegradedCore},
                                    {.id = "vehicle move", .flags = proto::kDegradedChosen}}});
  CHECK(degraded::Describe(wire) == "looks, puppet movement (core), vehicle move (chosen)");
  CHECK(degraded::Describe({}) == "nothing");
  registry.End("looks");
  CHECK(registry.Wire().entries.size() == 2);
}
