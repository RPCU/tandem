// tandem-report: what goes in the zip, and what is taken out of it.

#include "tandem/report.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>

#include <unistd.h>

using namespace tandem;

namespace {
struct Game {
  std::filesystem::path root;
  Game() {
    root = std::filesystem::temp_directory_path() / std::format("tandem-report-{}", getpid()) /
           "home" / "valerie" / "Cyberpunk 2077";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
  }
  ~Game() { std::filesystem::remove_all(root.parent_path().parent_path().parent_path()); }
  void Put(const std::string &file, std::string_view text) const {
    const auto path = root / file;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
  }
};

std::string Slurp(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace

TEST_CASE("paths, user names and addresses are taken out of a text") {
  const std::filesystem::path game = "C:\\Games\\Cyberpunk 2077";
  CHECK(report::Redact("loaded c:/games/cyberpunk 2077/red4ext/plugins/tandem/tandem.dll", game) ==
        "loaded <game>/red4ext/plugins/tandem/tandem.dll");
  CHECK(report::Redact("C:\\Users\\Valerie Doe\\AppData\\x and /home/valerie/.steam/y", {}) ==
        "C:\\Users\\<user>\\AppData\\x and /home/<user>/.steam/y");
  CHECK(report::Redact("guest 203.0.113.7:27450 joined, relay 198.51.100.20.", {}) ==
        "guest 203.0.x.x:27450 joined, relay 198.51.x.x.");
  // Not addresses: the loopback, versions, numbers with dots.
  CHECK(report::Redact("127.0.0.1 game 3.0.88.46235 codeware 1.21.2 at 1.5 s, 300.1.2.3", {}) ==
        "127.0.0.1 game 3.0.88.46235 codeware 1.21.2 at 1.5 s, 300.1.2.3");
  CHECK(report::Redact("", {}).empty());
}

TEST_CASE("tandem.ini loses its token and join code, commented out or not") {
  const auto ini = report::RedactIni("role = host\r\n"
                                     "address = 203.0.113.7\n"
                                     "relay = relay.example.org:27460\n"
                                     "relay_token = 00112233445566778899aabbccddeeff\n"
                                     "; relay_token=deadbeef\n"
                                     "join = K7Q-2M9\n"
                                     "name = V",
                                     {});
  CHECK(ini == "role = host\n"
               "address = 203.0.x.x\n"
               "relay = relay.example.org:27460\n"
               "relay_token = <removed>\n"
               "; relay_token= <removed>\n"
               "join = <removed>\n"
               "name = V\n");
}

TEST_CASE("the end of a text, by lines") {
  CHECK(report::TailLines("a\nb\nc\n", 2) == "b\nc\n");
  CHECK(report::TailLines("a\nb\nc", 2) == "b\nc");
  CHECK(report::TailLines("a\nb\n", 5) == "a\nb\n");
  CHECK(report::TailLines("", 3).empty());
}

TEST_CASE("the report holds the newest logs, the ini without secrets, and nothing else") {
  const Game game;
  game.Put("red4ext/logs/tandem-2026-11-18-20-00-00.log", "older session\n");
  game.Put("red4ext/logs/tandem-2026-11-19-21-00-00.log",
           std::format("loaded from {}\nguest 203.0.113.7 joined\n", game.root.string()));
  game.Put("red4ext/logs/red4ext-2026-11-19-21-00-00.log", "tandem loaded\n");
  game.Put("red4ext/logs/codeware-2026-11-19.log", "not ours\n");
  game.Put("r6/logs/redscript_rCURRENT.log", "[INFO] compiled\n");
  game.Put("red4ext/plugins/tandem/tandem.ini", "role = off\nrelay_token = abcdef\n");
  game.Put("red4ext/plugins/tandem/last-session.txt", "role: host\n");
  game.Put("red4ext/plugins/tandem/recent.txt", "203.0.113.7:27450 Jackie\n");
  game.Put("red4ext/plugins/tandem/recordings/a.tplay", "binary");
  // The older log is older for the file system too.
  std::filesystem::last_write_time(game.root / "red4ext/logs/tandem-2026-11-18-20-00-00.log",
                                   std::filesystem::file_time_type::clock::now() -
                                       std::chrono::hours{24});

  const auto out = game.root / "red4ext" / "plugins" / "tandem" / "reports";
  const auto result =
      report::Write({.game = game.root, .out = out, .versions = "tandem 0.2.0\n", .stamp = "test"});
  REQUIRE_MESSAGE(!result.zip.empty(), result.error);
  CHECK(result.zip == out / "tandem-report-test.zip");
  CHECK(result.included ==
        std::vector<std::string>{"versions.txt", "tandem.log", "tandem.previous.log", "red4ext.log",
                                 "redscript.log", "tandem.ini", "last-session.txt"});

  const auto zip = Slurp(result.zip);
  CHECK(zip.find("guest 203.0.x.x joined") != std::string::npos);
  CHECK(zip.find("loaded from <game>") != std::string::npos);
  CHECK(zip.find("older session") != std::string::npos);
  CHECK(zip.find("relay_token = <removed>") != std::string::npos);
  CHECK(zip.find("abcdef") == std::string::npos);
  CHECK(zip.find("203.0.113.7") == std::string::npos);
  CHECK(zip.find("valerie") == std::string::npos);
  CHECK(zip.find("not ours") == std::string::npos);
  CHECK(zip.find("Jackie") == std::string::npos);
  CHECK(zip.find("binary") == std::string::npos);

  // A real unzip reads it, when there is one.
  if (std::system("command -v unzip >/dev/null 2>&1") == 0) {
    const auto command = std::format("unzip -tq '{}' >/dev/null 2>&1", result.zip.string());
    CHECK(std::system(command.c_str()) == 0);
  }
}

TEST_CASE("a game with nothing to report says so and writes no file") {
  const Game game;
  const auto result = report::Write({.game = game.root, .out = game.root / "reports"});
  CHECK(result.zip.empty());
  CHECK_FALSE(result.error.empty());
  CHECK_FALSE(std::filesystem::exists(game.root / "reports" / "tandem-report-.zip"));
}

TEST_CASE("a session's summary says how it went, in lines") {
  const auto text = report::Format({.build = "0.2.0+aaaaaaa",
                                    .role = "join",
                                    .relay = true,
                                    .startedUtc = "2026-11-19 20:00:00",
                                    .seconds = 3725,
                                    .peakPlayers = 2,
                                    .ended = "the host is gone",
                                    .rttMs = 31,
                                    .lossPercent = 0.5f,
                                    .mismatches = 2,
                                    .resyncs = 1,
                                    .notes = {"Vee has other mods than you"}});
  CHECK(text == "tandem: 0.2.0+aaaaaaa\n"
                "role: join (through the relay)\n"
                "started: 2026-11-19 20:00:00 UTC\n"
                "lasted: 62 min 5 s\n"
                "players at most: 2\n"
                "ended: the host is gone\n"
                "link to the host: 31 ms, 0.5% lost\n"
                "consistency: 2 digest mismatch(es), 1 resync(s)\n"
                "note: Vee has other mods than you\n");
}
