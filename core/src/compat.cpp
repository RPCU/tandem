#include "tandem/compat.hpp"

#include "tandem_build.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <system_error>

namespace tandem::compat {
namespace {
constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL, kFnvPrime = 0x100000001b3ULL;

void Mix(std::uint64_t &hash, std::string_view bytes) {
  for (const char c : bytes) {
    hash ^= static_cast<unsigned char>(c);
    hash *= kFnvPrime;
  }
  // A separator no name or file holds: "ab" + "c" is not "a" + "bc".
  hash ^= 0xff;
  hash *= kFnvPrime;
}

std::string Lower(std::string text) {
  for (auto &c : text)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return text;
}

// A path as bytes (UTF-8), whatever the platform's own encoding.
std::string Bytes(const std::filesystem::path &path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char *>(text.data()), text.size()};
}

std::string_view ReleaseOf(std::string_view build) { return build.substr(0, build.find('+')); }

// The entries of one folder of a game, as "kind:name".
void List(const std::filesystem::path &folder, std::string_view kind, bool folders,
          std::string_view extension, std::vector<std::string> &out) {
  std::error_code ec;
  for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end;
       it.increment(ec)) {
    std::error_code ignored;
    if (folders && !it->is_directory(ignored))
      continue;
    auto name = Lower(Bytes(it->path().filename()));
    if (!extension.empty() && !name.ends_with(extension))
      continue;
    if (name == "tandem" || name.starts_with("tandem."))
      continue;
    out.push_back(std::format("{}:{}", kind, name));
  }
}
} // namespace

std::optional<Version> ParseVersion(std::string_view text) {
  if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
    text.remove_prefix(1);
  Version version;
  std::uint16_t *parts[] = {&version.a, &version.b, &version.c, &version.d};
  std::size_t count = 0;
  while (!text.empty() && count < 4) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), *parts[count]);
    if (ec != std::errc{})
      return std::nullopt;
    ++count;
    text.remove_prefix(static_cast<std::size_t>(end - text.data()));
    if (text.empty())
      break;
    if (text.front() != '.')
      return std::nullopt;
    text.remove_prefix(1);
    if (text.empty())
      return std::nullopt;
  }
  if (count == 0 || !text.empty())
    return std::nullopt;
  return version;
}

std::string Format(const Version &version) {
  if (version.d != 0)
    return std::format("{}.{}.{}.{}", version.a, version.b, version.c, version.d);
  return std::format("{}.{}.{}", version.a, version.b, version.c);
}

const char *Build() { return TANDEM_BUILD; }
const char *Release() { return TANDEM_RELEASE; }

bool SameBuild(std::string_view a, std::string_view b) {
  if (a == b)
    return true;
  const bool bare = a.find('+') == std::string_view::npos || b.find('+') == std::string_view::npos;
  return bare && ReleaseOf(a) == ReleaseOf(b);
}

Startup Check(const Facts &facts) {
  Startup out;
  if (!facts.game)
    out.warnings.push_back(std::format(
        "tandem could not read the game's version (it is built for patch {})", kGamePatch));
  else if (*facts.game != kGame)
    out.problems.push_back(std::format(
        "This tandem is built for Cyberpunk 2077 patch {} ({}); this game is {}. Co-op is off: "
        "install the tandem release made for your game.",
        kGamePatch, Format(kGame), Format(*facts.game)));

  for (const auto &need : kRequirements) {
    const auto found = std::ranges::find(facts.mods, need.name, &Found::name);
    if (found == facts.mods.end() || !found->present) {
      if (need.needed)
        out.problems.push_back(
            std::format("{} is not installed ({}). tandem needs {} {} or newer. Co-op is off.",
                        need.name, need.dll, need.name, Format(need.minimum)));
      continue;
    }
    if (!found->version) {
      if (need.needed)
        out.warnings.push_back(std::format("tandem could not read {}'s version (it needs {} or "
                                           "newer)",
                                           need.name, Format(need.minimum)));
      continue;
    }
    if (*found->version < need.minimum && need.needed)
      out.problems.push_back(
          std::format("{} {} is too old: tandem needs {} or newer. Co-op is off.", need.name,
                      Format(*found->version), Format(need.minimum)));
  }
  // Only said: without redscript no script of ours runs to show a message,
  // and the plugin does nothing without its scripts.
  if (!facts.redscript)
    out.warnings.push_back(std::format(
        "redscript does not seem installed ({} is missing): without redscript {} or newer "
        "tandem's menu and scripts do not load",
        kRedscript, kRedscriptVersion));
  if (!facts.scripts)
    out.problems.push_back("tandem's scripts are missing (r6/scripts/tandem): install the whole "
                           "release again. Co-op is off.");
  if (!facts.tweaks)
    out.problems.push_back("tandem's tweaks are missing (r6/tweaks/tandem): install the whole "
                           "release again. Co-op is off.");
  return out;
}

std::uint64_t HashTree(const std::filesystem::path &folder) {
  std::vector<std::pair<std::string, std::filesystem::path>> files;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(folder, ec), end; !ec && it != end;
       it.increment(ec)) {
    std::error_code ignored;
    if (!it->is_regular_file(ignored))
      continue;
    files.emplace_back(Lower(Bytes(it->path().lexically_relative(folder))), it->path());
  }
  if (files.empty())
    return 0;
  std::ranges::sort(files);
  auto hash = kFnvOffset;
  for (const auto &[name, path] : files) {
    Mix(hash, name);
    std::ifstream file(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::erase(content, '\r');
    Mix(hash, content);
  }
  return hash == 0 ? 1 : hash;
}

Mods InstalledMods(const std::filesystem::path &game) {
  Mods mods;
  List(game / "red4ext" / "plugins", "red4ext", true, "", mods.names);
  List(game / "archive" / "pc" / "mod", "archive", false, ".archive", mods.names);
  List(game / "r6" / "scripts", "script", false, "", mods.names);
  List(game / "r6" / "tweaks", "tweak", false, "", mods.names);
  List(game / "bin" / "x64" / "plugins" / "cyber_engine_tweaks" / "mods", "cet", true, "",
       mods.names);
  List(game / "mods", "redmod", true, "", mods.names);
  std::ranges::sort(mods.names);
  if (mods.names.empty())
    return mods;
  mods.hash = kFnvOffset;
  for (const auto &name : mods.names)
    Mix(mods.hash, name);
  if (mods.hash == 0)
    mods.hash = 1;
  return mods;
}

proto::Install LocalInstall(const std::filesystem::path &game, std::optional<Version> gameVersion) {
  proto::Install install{.build = Build()};
  if (gameVersion)
    install.game = Format(*gameVersion);
  if (game.empty())
    return install;
  install.scripts = HashTree(game / "r6" / "scripts" / "tandem");
  install.tweaks = HashTree(game / "r6" / "tweaks" / "tandem");
  const auto mods = InstalledMods(game);
  install.mods = mods.hash;
  install.modCount = static_cast<std::uint16_t>(std::min<std::size_t>(mods.names.size(), 0xffff));
  return install;
}

std::vector<std::string> Differences(const proto::Install &ours, const proto::Install &theirs,
                                     std::string_view who) {
  std::vector<std::string> out;
  const bool builds = !ours.build.empty() && !theirs.build.empty();
  if (builds && !SameBuild(ours.build, theirs.build))
    out.push_back(
        std::format("{} runs tandem {}, you run {}: install the same release on both games", who,
                    theirs.build, ours.build));
  if (!ours.game.empty() && !theirs.game.empty() && ours.game != theirs.game)
    out.push_back(std::format("{}'s game is version {}, yours is {}", who, theirs.game, ours.game));
  // Different builds have different files: said once, above.
  const bool sameBuild = !builds || SameBuild(ours.build, theirs.build);
  const bool scripts = ours.scripts && theirs.scripts && ours.scripts != theirs.scripts;
  const bool tweaks = ours.tweaks && theirs.tweaks && ours.tweaks != theirs.tweaks;
  if (sameBuild && (scripts || tweaks))
    out.push_back(std::format(
        "{}'s tandem {} differ from yours: install the whole release again on both games", who,
        scripts && tweaks ? "scripts and tweaks"
        : scripts         ? "scripts"
                          : "tweaks"));
  if (ours.mods && theirs.mods && ours.mods != theirs.mods)
    out.push_back(std::format("{} has other mods than you ({} against your {}): gameplay mods "
                              "that differ can put your games out of step",
                              who, theirs.modCount, ours.modCount));
  return out;
}

namespace {
constexpr std::string_view kSameRelease = "both players need the same tandem release";
}

std::string RefusalForGuest(std::uint16_t theirProtocol) {
  return std::format("your tandem speaks protocol {}, the host's ({}) protocol {}: {}",
                     theirProtocol, Build(), proto::kProtocolVersion, kSameRelease);
}

std::string RefusalForHost(std::string_view who, std::uint16_t theirProtocol,
                           std::string_view theirBuild) {
  return std::format("{} could not join: their tandem{} speaks protocol {}, yours ({}) protocol "
                     "{}: {}",
                     who.empty() ? std::string_view("A player") : who,
                     theirBuild.empty() ? std::string() : std::format(" ({})", theirBuild),
                     theirProtocol, Build(), proto::kProtocolVersion, kSameRelease);
}

std::string ExpansionProblem(Expansion state) {
  switch (state) {
  case Expansion::Disabled:
    return "Phantom Liberty is owned but not installed or not enabled in this game. tandem "
           "needs it on every player's game (the items, vehicles and Relic perks a V brings "
           "only exist with it). Install and enable it. Co-op is off.";
  case Expansion::Missing:
    return "This game does not have the Phantom Liberty expansion. tandem needs it on every "
           "player's game (the items, vehicles and Relic perks a V brings only exist with "
           "it). Co-op is off.";
  case Expansion::Unknown:
  case Expansion::Enabled:
    break;
  }
  return {};
}

std::string ExpansionRefusalForHost(std::string_view who, bool oursLacks) {
  return std::format("{} could not join: {} game has no Phantom Liberty (tandem needs the "
                     "expansion on every player's game)",
                     who.empty() ? std::string_view("A player") : who,
                     oursLacks ? "your" : "their");
}

bool IsFinalRefusal(std::string_view reason) {
  return reason.find(kSameRelease) != std::string_view::npos ||
         reason.find(kExpansionGuestLacks) != std::string_view::npos ||
         reason.find(kExpansionHostLacks) != std::string_view::npos;
}
} // namespace tandem::compat
