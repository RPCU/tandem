// The moment list without the game (docs/architecture/scripted-moments.md,
// "The moment list"): keys and lines as tandem.ini and moments.txt write
// them, and which entry decides for what the host's V is in.

#include "tandem/moments.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <string>

using namespace tandem;

namespace {
// A stand-in for the engine's ResourcePath hash: the path's length and its
// first letter, never 0.
std::uint64_t FakeHash(std::string_view path) {
  return path.empty() ? 0 : (path.size() << 8) | static_cast<unsigned char>(path.front());
}
} // namespace

TEST_CASE("moment list keys: scene and phase paths or hashes, a piece of the journal path") {
  using moments::Kind;
  using moments::Verdict;
  std::string error;
  const auto scene = moments::ParseKey(R"( scene:base\quest\main_quests\q005\scenes\x.scene )",
                                       Verdict::Watch, FakeHash, error);
  REQUIRE(scene);
  CHECK(scene->kind == Kind::Scene);
  CHECK(scene->hash == FakeHash(R"(base\quest\main_quests\q005\scenes\x.scene)"));
  const auto phase = moments::ParseKey("PHASE:0x00ab12", Verdict::Play, FakeHash, error);
  REQUIRE(phase);
  CHECK(phase->kind == Kind::Phase);
  CHECK(phase->hash == 0xab12);
  const auto quest = moments::ParseKey("quest:Q101_", Verdict::Watch, FakeHash, error);
  REQUIRE(quest);
  CHECK(quest->kind == Kind::Quest);
  CHECK(quest->text == "q101_");

  for (const auto *bad : {"q101_", "quest:", "npc:jackie", "scene:"}) {
    error.clear();
    CHECK_FALSE(moments::ParseKey(bad, Verdict::Watch, FakeHash, error));
    CHECK_FALSE(error.empty());
  }
  // A path with no hash (no hasher): refused, not a match for everything.
  CHECK_FALSE(moments::ParseKey("scene:a.scene", Verdict::Watch, nullptr, error));
}

TEST_CASE("moments.txt lines: a verdict, a key, a comment") {
  std::string error;
  const auto line =
      moments::ParseLine("watch quest:q005_heist  # the AV crash: no seat", FakeHash, error);
  REQUIRE(line);
  CHECK(line->verdict == moments::Verdict::Watch);
  CHECK(line->note == "the AV crash: no seat");
  CHECK_FALSE(moments::ParseLine("   # only a comment", FakeHash, error));
  CHECK(error.empty());
  CHECK_FALSE(moments::ParseLine("", FakeHash, error));
  CHECK(error.empty());
  CHECK_FALSE(moments::ParseLine("skip quest:q001", FakeHash, error));
  CHECK_FALSE(error.empty());
  CHECK_FALSE(moments::ParseLine("watch", FakeHash, error));
  CHECK_FALSE(error.empty());
}

TEST_CASE("the list's verdict: the scene before the phase before the quest, watch on a tie") {
  using moments::Verdict;
  std::string error;
  moments::List list;
  for (const auto *line : {"watch quest:q005_", "play phase:0x20", "watch phase:0x21",
                           "play scene:0x30", "watch scene:0x31", "play scene:0x31"})
    list.Add(*moments::ParseLine(line, FakeHash, error));
  CHECK(list.Size() == 6);
  CHECK(list.HasPhases());

  const moments::Entry *matched = nullptr;
  CHECK(list.Of({}) == Verdict::None);
  CHECK(list.Of({.quest = "quests/main_quest/prologue/Q005_heist/escape"}, &matched) ==
        Verdict::Watch);
  REQUIRE(matched);
  CHECK(matched->kind == moments::Kind::Quest);
  // A phase in that quest marked play: the phase decides.
  CHECK(list.Of({.phases = {0x99, 0x20}, .quest = "q005_heist"}) == Verdict::Play);
  CHECK(list.Of({.phases = {0x20, 0x21}}) == Verdict::Watch);
  // The scene decides over both.
  CHECK(list.Of({.scene = 0x30, .phases = {0x21}, .quest = "q005_heist"}) == Verdict::Play);
  CHECK(list.Of({.scene = 0x31}) == Verdict::Watch);
  CHECK(list.Of({.scene = 0x32, .phases = {0x22}, .quest = "q101_"}, &matched) == Verdict::None);
  CHECK(matched == nullptr);

  moments::List quests;
  quests.Add(*moments::ParseLine("watch quest:q101", FakeHash, error));
  CHECK_FALSE(quests.HasPhases());
}

TEST_CASE("the shipped moments.txt parses whole") {
  std::ifstream file(TANDEM_SOURCE_DIR "/dist/moments.txt");
  REQUIRE(file.good());
  std::string line;
  int number = 0;
  while (std::getline(file, line)) {
    ++number;
    std::string error;
    moments::ParseLine(line, FakeHash, error);
    INFO("line " << number << ": " << line);
    CHECK(error.empty());
  }
  CHECK(number > 0);
}
