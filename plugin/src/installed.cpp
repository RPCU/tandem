#include "installed.hpp"

#include <windows.h>

#include <winver.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace tandem::plugin {
namespace {
bool HasFiles(const std::filesystem::path &folder) {
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(folder, ec), end; !ec && it != end;
       it.increment(ec)) {
    std::error_code ignored;
    if (it->is_regular_file(ignored))
      return true;
  }
  return false;
}

std::filesystem::path Under(const std::filesystem::path &game, std::string_view relative) {
  return game / std::filesystem::path(relative);
}
} // namespace

std::filesystem::path GameDirectoryOf(const std::filesystem::path &pluginDirectory) {
  return pluginDirectory.parent_path().parent_path().parent_path();
}

std::filesystem::path ModulePath(void *module) {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(static_cast<HMODULE>(module), path, MAX_PATH);
  return path;
}

std::optional<compat::Version> FileVersionOf(const std::filesystem::path &file) {
  DWORD ignored = 0;
  const auto size = GetFileVersionInfoSizeW(file.c_str(), &ignored);
  if (size == 0)
    return std::nullopt;
  std::vector<std::byte> data(size);
  if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data()))
    return std::nullopt;
  VS_FIXEDFILEINFO *info = nullptr;
  UINT length = 0;
  if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&info), &length) || !info ||
      length < sizeof(VS_FIXEDFILEINFO))
    return std::nullopt;
  return compat::Version{static_cast<std::uint16_t>(HIWORD(info->dwFileVersionMS)),
                         static_cast<std::uint16_t>(LOWORD(info->dwFileVersionMS)),
                         static_cast<std::uint16_t>(HIWORD(info->dwFileVersionLS)),
                         static_cast<std::uint16_t>(LOWORD(info->dwFileVersionLS))};
}

compat::Facts GatherFacts(const std::filesystem::path &game,
                          const std::filesystem::path &executable) {
  compat::Facts facts;
  facts.game = FileVersionOf(executable);
  for (const auto &need : compat::kRequirements) {
    const auto dll = Under(game, need.dll);
    std::error_code ec;
    compat::Found found{.name = std::string(need.name),
                        .present = std::filesystem::exists(dll, ec)};
    if (found.present)
      found.version = FileVersionOf(dll);
    facts.mods.push_back(std::move(found));
  }
  std::error_code ec;
  facts.redscript = std::filesystem::exists(Under(game, compat::kRedscript), ec);
  facts.scripts = HasFiles(game / "r6" / "scripts" / "tandem");
  facts.tweaks = HasFiles(game / "r6" / "tweaks" / "tandem");
  return facts;
}

std::string VersionsText(const compat::Facts &facts, const compat::Startup &startup,
                         const proto::Install &install, const std::vector<std::string> &notes) {
  std::string out;
  const auto line = [&](std::string text) { out += text + "\r\n"; };
  line(std::format("written: {:%Y-%m-%d %H:%M:%S} UTC",
                   std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())));
  line(std::format("tandem: {} (protocol {})", install.build, proto::kProtocolVersion));
  line(std::format("built for: Cyberpunk 2077 patch {} ({})", compat::kGamePatch,
                   compat::Format(compat::kGame)));
  line(std::format("game: {}", facts.game ? compat::Format(*facts.game) : "version not read"));
  for (const auto &need : compat::kRequirements) {
    const auto found = std::ranges::find(facts.mods, need.name, &compat::Found::name);
    const auto state = found == facts.mods.end() || !found->present ? std::string("not installed")
                       : found->version ? compat::Format(*found->version)
                                        : std::string("installed, version not read");
    line(std::format("{}: {} ({} {})", need.name, state,
                     need.needed ? "needs" : "optional, made with", compat::Format(need.minimum)));
  }
  line(std::format("redscript: {} (made with {})", facts.redscript ? "installed" : "not installed",
                   compat::kRedscriptVersion));
  line(std::format("tandem scripts: {} (hash {:016x})", facts.scripts ? "installed" : "missing",
                   install.scripts));
  line(std::format("tandem tweaks: {} (hash {:016x})", facts.tweaks ? "installed" : "missing",
                   install.tweaks));
  line(std::format("other mods: {} (hash {:016x}; names are not reported)", install.modCount,
                   install.mods));
  line("");
  line(startup.Blocked() ? "startup check: co-op is off" : "startup check: passed");
  for (const auto &problem : startup.problems)
    line(std::format("  problem: {}", problem));
  for (const auto &warning : startup.warnings)
    line(std::format("  warning: {}", warning));
  if (!notes.empty()) {
    line("");
    line("against the other players, this run:");
    for (const auto &note : notes)
      line(std::format("  {}", note));
  }
  return out;
}
} // namespace tandem::plugin
