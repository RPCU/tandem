#pragma once

// RED4ext's logger (red4ext/logs/tandem-*.log), usable from any thread. With
// TANDEM_LOG (tandem/instance.hpp) every line also goes to a file of our own:
// one game among several on a machine is then told apart by its log's name.
//
// Levels (tandem.ini `log`, docs/architecture/netcode.md): `info`, the
// default, is quiet: the session's life (joins, leaves, ends), warnings and
// errors, a minute's summary of the traffic and the frame cost. `debug` adds
// what happens to each puppet, proxy, hit and shot (the in-game scenarios
// read those: tools/game.sh's test roles set it); `warn` only problems.

#include <RED4ext/Api/v1/Logger.hpp>
#include <RED4ext/Api/v1/PluginHandle.hpp>

#include <atomic>
#include <filesystem>
#include <format>
#include <string>

namespace tandem::plugin::log {
inline RED4ext::v1::PluginHandle g_handle{};
inline const RED4ext::v1::Logger *g_logger{};

enum class Level : int { Error = 0, Warn = 1, Info = 2, Debug = 3 };
inline std::atomic<int> g_level{static_cast<int>(Level::Info)};
inline void SetLevel(Level level) { g_level.store(static_cast<int>(level)); }
inline bool Enabled(Level level) {
  return static_cast<int>(level) <= g_level.load(std::memory_order_relaxed);
}

// The log file of our own (started empty); false when it cannot be written.
bool OpenFile(const std::filesystem::path &path);
void CloseFile();
// A line to it, when there is one.
void ToFile(const char *level, const std::string &message);

// An info line whatever the level (asked for: tools/game.sh cmd perf).
template <class... Args> void Always(std::format_string<Args...> format, Args &&...args) {
  if (!g_logger)
    return;
  const auto message = std::format(format, std::forward<Args>(args)...);
  g_logger->Info(g_handle, message.c_str());
  ToFile("info", message);
}

// Details, at `log = debug` only. Written as info lines: RED4ext's own level
// filter must not hide them.
template <class... Args> void Debug(std::format_string<Args...> format, Args &&...args) {
  if (!g_logger || !Enabled(Level::Debug))
    return;
  const auto message = std::format(format, std::forward<Args>(args)...);
  g_logger->Info(g_handle, message.c_str());
  ToFile("debug", message);
}

template <class... Args> void Info(std::format_string<Args...> format, Args &&...args) {
  if (!g_logger || !Enabled(Level::Info))
    return;
  const auto message = std::format(format, std::forward<Args>(args)...);
  g_logger->Info(g_handle, message.c_str());
  ToFile("info", message);
}

template <class... Args> void Warn(std::format_string<Args...> format, Args &&...args) {
  if (!g_logger || !Enabled(Level::Warn))
    return;
  const auto message = std::format(format, std::forward<Args>(args)...);
  g_logger->Warn(g_handle, message.c_str());
  ToFile("warning", message);
}

template <class... Args> void Error(std::format_string<Args...> format, Args &&...args) {
  if (!g_logger)
    return;
  const auto message = std::format(format, std::forward<Args>(args)...);
  g_logger->Error(g_handle, message.c_str());
  ToFile("error", message);
}
} // namespace tandem::plugin::log
