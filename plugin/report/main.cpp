// tandem-report.exe: the bug report when the game does not start (when it
// does, the co-op menu's "Write a bug report" writes the same zip).
//
// Double-click it where it was installed (red4ext/plugins/tandem), or give
// it the game's folder: tandem-report.exe "D:\Games\Cyberpunk 2077". It
// writes reports/tandem-report-DATE.zip next to itself and says what it
// put in it (tandem/report.hpp has the list). It sends nothing anywhere.

#include "../src/installed.hpp"

#include "tandem/compat.hpp"
#include "tandem/report.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {
std::string Shown(const std::filesystem::path &path) {
  const auto text = path.u8string();
  return {reinterpret_cast<const char *>(text.data()), text.size()};
}

int Finish(int code, bool wait) {
  if (wait) {
    std::puts("\nPress Enter to close.");
    std::getchar();
  }
  return code;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
  using namespace tandem;
  bool wait = true;
  std::filesystem::path game;
  for (int i = 1; i < argc; ++i) {
    if (std::wstring_view(argv[i]) == L"--quiet")
      wait = false;
    else
      game = argv[i];
  }
  const auto here = plugin::ModulePath(nullptr).parent_path();
  if (game.empty())
    game = plugin::GameDirectoryOf(here);
  const auto executable = game / "bin" / "x64" / "Cyberpunk2077.exe";
  std::error_code ec;
  if (!std::filesystem::exists(executable, ec)) {
    std::printf("No game found in %s.\n"
                "Run tandem-report.exe from red4ext\\plugins\\tandem inside the game's folder,\n"
                "or give it the game's folder: tandem-report.exe \"D:\\Games\\Cyberpunk 2077\"\n",
                Shown(game).c_str());
    return Finish(2, wait);
  }

  const auto facts = plugin::GatherFacts(game, executable);
  const auto startup = compat::Check(facts);
  const auto install = compat::LocalInstall(game, facts.game);
  const auto result =
      report::Write({.game = game,
                     .out = game / "red4ext" / "plugins" / "tandem" / "reports",
                     .versions = plugin::VersionsText(facts, startup, install, {})});
  if (result.zip.empty()) {
    std::printf("No report written: %s\n", result.error.c_str());
    return Finish(1, wait);
  }
  std::printf("tandem %s\n", compat::Build());
  for (const auto &problem : startup.problems)
    std::printf("Problem: %s\n", problem.c_str());
  std::printf("\nReport written:\n  %s\n\nIt holds:\n", Shown(result.zip).c_str());
  for (const auto &name : result.included)
    std::printf("  %s\n", name.c_str());
  std::puts("\nPaths, user names and network addresses are masked; the relay token and the\n"
            "join code are removed from tandem.ini. Attach the zip to your bug report.");
  return Finish(0, wait);
}
