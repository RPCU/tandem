#pragma once

// What is installed around us, read from the game's folder (Windows file
// versions): the facts tandem/compat.hpp judges at startup, and the
// versions.txt of a bug report. No engine access: tandem.dll and
// tandem-report.exe share it.

#include "tandem/compat.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tandem::plugin {
// <game>/red4ext/plugins/tandem -> <game>.
std::filesystem::path GameDirectoryOf(const std::filesystem::path &pluginDirectory);
// The module `module` was loaded from (nullptr: the running executable).
std::filesystem::path ModulePath(void *module);
// A DLL's or an executable's file version (none: no such file, or it has
// no version resource).
std::optional<compat::Version> FileVersionOf(const std::filesystem::path &file);
// The game's version (`executable`) and the mods compat::kRequirements names.
compat::Facts GatherFacts(const std::filesystem::path &game,
                          const std::filesystem::path &executable);
// versions.txt: everything above in lines, with what the startup check
// said and `notes` (what differed from the other players, this run).
std::string VersionsText(const compat::Facts &facts, const compat::Startup &startup,
                         const proto::Install &install, const std::vector<std::string> &notes);
} // namespace tandem::plugin
