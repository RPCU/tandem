// Startup checks, what two installations differ in, and what both players
// read when their versions do not match.

#include "ports.hpp"
#include "tandem/compat.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>

#include <unistd.h>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
// A folder of its own under the system's temporary one, gone with the test.
struct Scratch {
  std::filesystem::path root;
  explicit Scratch(const char *name) {
    root = std::filesystem::temp_directory_path() / std::format("tandem-{}-{}", name, getpid());
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
  }
  ~Scratch() { std::filesystem::remove_all(root); }
  void Put(const std::string &file, std::string_view text) const {
    const auto path = root / file;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
  }
};

compat::Facts Good() {
  compat::Facts facts{.game = compat::kGame};
  for (const auto &need : compat::kRequirements)
    facts.mods.push_back({std::string(need.name), true, need.minimum});
  return facts;
}

bool Mentions(const std::vector<std::string> &lines, std::string_view word) {
  for (const auto &line : lines)
    if (line.find(word) != std::string::npos)
      return true;
  return false;
}
} // namespace

TEST_CASE("versions are read with or without a v, in two to four parts") {
  CHECK(compat::ParseVersion("1.21.2") == compat::Version{1, 21, 2, 0});
  CHECK(compat::ParseVersion("v1.12") == compat::Version{1, 12, 0, 0});
  CHECK(compat::ParseVersion("3.0.88.46235") == compat::kGame);
  CHECK_FALSE(compat::ParseVersion(""));
  CHECK_FALSE(compat::ParseVersion("1."));
  CHECK_FALSE(compat::ParseVersion("1.2.3.4.5"));
  CHECK_FALSE(compat::ParseVersion("one"));
  CHECK(compat::Format({1, 21, 2, 0}) == "1.21.2");
  CHECK(compat::Format(compat::kGame) == "3.0.88.46235");
  CHECK(compat::Version{1, 21, 2, 0} < compat::Version{1, 21, 10, 0});
}

TEST_CASE("the game and the mods this build was made for pass the startup check") {
  const auto startup = compat::Check(Good());
  CHECK_FALSE(startup.Blocked());
  CHECK(startup.warnings.empty());
}

TEST_CASE("another game version turns co-op off and says both versions") {
  auto facts = Good();
  facts.game = compat::Version{3, 0, 80, 51928};
  const auto startup = compat::Check(facts);
  REQUIRE(startup.problems.size() == 1);
  CHECK(Mentions(startup.problems, "2.50"));
  CHECK(Mentions(startup.problems, "3.0.80.51928"));
}

TEST_CASE("a missing or outdated mod turns co-op off; an optional one never does") {
  auto facts = Good();
  facts.mods[1].version = compat::Version{1, 20, 5, 0}; // Codeware
  facts.mods[2].present = false;                        // TweakXL
  facts.mods[3].present = false;                        // ArchiveXL: not needed
  const auto startup = compat::Check(facts);
  REQUIRE(startup.problems.size() == 2);
  CHECK(Mentions(startup.problems, "Codeware 1.20.5 is too old"));
  CHECK(Mentions(startup.problems, "TweakXL is not installed"));
  CHECK_FALSE(Mentions(startup.problems, "ArchiveXL"));
}

TEST_CASE("what cannot be read is a warning, not a problem") {
  auto facts = Good();
  facts.game.reset();
  facts.mods[1].version.reset();
  const auto startup = compat::Check(facts);
  CHECK_FALSE(startup.Blocked());
  CHECK(startup.warnings.size() == 2);
}

TEST_CASE("missing tandem files turn co-op off") {
  auto facts = Good();
  facts.tweaks = false;
  facts.redscript = false;
  const auto startup = compat::Check(facts);
  CHECK(startup.problems.size() == 2);
  CHECK(Mentions(startup.problems, "r6/tweaks/tandem"));
  CHECK(Mentions(startup.problems, "redscript"));
}

TEST_CASE("a folder's hash follows names and contents, not line endings or order") {
  const Scratch a("hash-a"), b("hash-b");
  CHECK(compat::HashTree(a.root) == 0);
  CHECK(compat::HashTree(a.root / "nowhere") == 0);
  a.Put("One.reds", "func A() {}\nfunc B() {}\n");
  a.Put("sub/Two.reds", "x");
  b.Put("sub/two.reds", "x");
  b.Put("one.reds", "func A() {}\r\nfunc B() {}\r\n");
  const auto hash = compat::HashTree(a.root);
  CHECK(hash != 0);
  CHECK(compat::HashTree(b.root) == hash);
  b.Put("one.reds", "func A() {}\nfunc C() {}\n");
  CHECK(compat::HashTree(b.root) != hash);
  a.Put("Three.reds", "");
  CHECK(compat::HashTree(a.root) != hash);
}

TEST_CASE("the other mods are listed by name, tandem's own left out") {
  const Scratch game("mods");
  game.Put("red4ext/plugins/Codeware/Codeware.dll", "");
  game.Put("red4ext/plugins/tandem/tandem.dll", "");
  game.Put("archive/pc/mod/Nice Hair.archive", "");
  game.Put("archive/pc/mod/readme.txt", "");
  game.Put("r6/scripts/tandem/System.reds", "");
  game.Put("r6/scripts/Other/Other.reds", "");
  game.Put("r6/tweaks/tandem/puppets.yaml", "");
  game.Put("bin/x64/plugins/cyber_engine_tweaks/mods/SimpleMenu/init.lua", "");
  const auto mods = compat::InstalledMods(game.root);
  CHECK(mods.names == std::vector<std::string>{"archive:nice hair.archive", "cet:simplemenu",
                                               "red4ext:codeware", "script:other"});
  CHECK(mods.hash != 0);

  const auto install = compat::LocalInstall(game.root, compat::kGame);
  CHECK(install.build == compat::Build());
  CHECK(install.game == "3.0.88.46235");
  CHECK(install.scripts != 0);
  CHECK(install.tweaks != 0);
  CHECK(install.mods == mods.hash);
  CHECK(install.modCount == 4);

  game.Put("archive/pc/mod/More.archive", "");
  CHECK(compat::InstalledMods(game.root).hash != mods.hash);
  CHECK(compat::InstalledMods(game.root / "nowhere").hash == 0);
}

TEST_CASE("two games are compared in words; what is unknown is not compared") {
  const proto::Install ours{.build = "0.2.0+aaaaaaa",
                            .game = "3.0.88.46235",
                            .scripts = 1,
                            .tweaks = 2,
                            .mods = 3,
                            .modCount = 10};
  CHECK(compat::Differences(ours, ours, "Jackie").empty());
  // The mock player: a build, nothing else.
  CHECK(compat::Differences(ours, {.build = "0.2.0+aaaaaaa"}, "Jackie").empty());
  // A build made outside git is the same release.
  CHECK(compat::Differences(ours, {.build = "0.2.0"}, "Jackie").empty());

  auto theirs = ours;
  theirs.scripts = 9;
  auto lines = compat::Differences(ours, theirs, "Jackie");
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] ==
        "Jackie's tandem scripts differ from yours: install the whole release again on both games");

  theirs = ours;
  theirs.mods = 99;
  theirs.modCount = 12;
  lines = compat::Differences(ours, theirs, "Jackie");
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].find("Jackie has other mods than you (12 against your 10)") == 0);

  // Another build: said once, its files are not said to differ as well.
  theirs = ours;
  theirs.build = "0.3.0+bbbbbbb";
  theirs.scripts = 9;
  theirs.game = "3.0.80.51928";
  lines = compat::Differences(ours, theirs, "Jackie");
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].find("Jackie runs tandem 0.3.0+bbbbbbb, you run 0.2.0+aaaaaaa") == 0);
  CHECK(lines[1] == "Jackie's game is version 3.0.80.51928, yours is 3.0.88.46235");
}

TEST_CASE("a protocol mismatch is worded for both players and is final") {
  const auto guest = compat::RefusalForGuest(5);
  CHECK(guest.find("your tandem speaks protocol 5") == 0);
  CHECK(guest.find(std::format("protocol {}", proto::kProtocolVersion)) != std::string::npos);
  CHECK(compat::IsVersionRefusal("refused: " + guest));
  CHECK_FALSE(compat::IsVersionRefusal("refused: the session is full"));
  const auto host = compat::RefusalForHost("Jackie", 5, "0.1.0+abc");
  CHECK(host.find("Jackie could not join: their tandem (0.1.0+abc) speaks protocol 5") == 0);
}

TEST_CASE("a game with another protocol is refused in words, and the host is told") {
  const auto port = test::NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto raw = net::Session::Connect("127.0.0.1", port, error);
  REQUIRE_MESSAGE(raw, error);

  std::optional<std::string> refusal, notice;
  bool welcomed = false;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!(refusal && notice) && std::chrono::steady_clock::now() < deadline) {
    for (const auto &event : raw->Poll(1ms)) {
      if (event.kind == net::Event::Kind::Connected)
        raw->Send(event.peer,
                  proto::Hello{.version = static_cast<std::uint16_t>(proto::kProtocolVersion - 1),
                               .name = "Jackie",
                               .gameVersion = "0.1.0+0ld"});
      if (event.kind != net::Event::Kind::Received)
        continue;
      welcomed |= std::holds_alternative<proto::Welcome>(*event.message);
      if (const auto *refused = std::get_if<proto::Refused>(&*event.message))
        refusal = refused->reason;
    }
    for (const auto &event : host->Update(1ms))
      if (const auto *refused = std::get_if<events::JoinRefused>(&event))
        notice = refused->notice;
  }
  REQUIRE(refusal);
  REQUIRE(notice);
  CHECK_FALSE(welcomed);
  CHECK(compat::IsVersionRefusal(*refusal));
  CHECK(notice->find("Jackie could not join: their tandem (0.1.0+0ld)") == 0);
  CHECK(host->Players().size() == 1);
}

TEST_CASE("each side learns what the other has installed and what differs") {
  const auto port = test::NextPort();
  std::string error;
  const proto::Install hostHas{.build = "0.2.0+aaaaaaa", .scripts = 1, .mods = 5, .modCount = 3};
  const proto::Install guestHas{.build = "0.2.0+aaaaaaa", .scripts = 2, .mods = 5, .modCount = 3};
  auto host = Endpoint::Host({"Vee", {}, hostHas}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto guest = Endpoint::Join({"Jackie", {}, guestHas}, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(guest, error);

  std::optional<events::RemoteInstall> atHost, atGuest;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!(atHost && atGuest) && std::chrono::steady_clock::now() < deadline) {
    for (const auto &event : host->Update(1ms))
      if (const auto *install = std::get_if<events::RemoteInstall>(&event))
        atHost = *install;
    for (const auto &event : guest->Update(1ms))
      if (const auto *install = std::get_if<events::RemoteInstall>(&event))
        atGuest = *install;
  }
  REQUIRE(atHost);
  REQUIRE(atGuest);
  CHECK(atHost->install == guestHas);
  CHECK(atHost->player == guest->You());
  REQUIRE(atHost->differences.size() == 1);
  CHECK(atHost->differences[0].find("Jackie's tandem scripts differ") == 0);
  CHECK(atGuest->install == hostHas);
  CHECK(atGuest->player == guest->HostPlayer());
  REQUIRE(atGuest->differences.size() == 1);
  CHECK(atGuest->differences[0].find("Vee's tandem scripts differ") == 0);
  // Told, and still playing together.
  CHECK(guest->IsJoined());
  CHECK(host->Players().size() == 2);
}

TEST_CASE("games that say nothing but their build have nothing to differ in") {
  const auto port = test::NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error);
  REQUIRE((host && guest));
  std::optional<events::RemoteInstall> atHost;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!atHost && std::chrono::steady_clock::now() < deadline) {
    for (const auto &event : host->Update(1ms))
      if (const auto *install = std::get_if<events::RemoteInstall>(&event))
        atHost = *install;
    guest->Update(1ms);
  }
  REQUIRE(atHost);
  CHECK(atHost->install.build == compat::Build());
  CHECK(atHost->differences.empty());
}
