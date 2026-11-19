#pragma once

// RED4ext's logger (red4ext/logs/tandem-*.log), usable from any thread. With
// TANDEM_LOG (tandem/instance.hpp) every line also goes to a file of our own:
// one game among several on a machine is then told apart by its log's name.

#include <RED4ext/Api/v1/Logger.hpp>
#include <RED4ext/Api/v1/PluginHandle.hpp>

#include <filesystem>
#include <format>
#include <string>

namespace tandem::plugin::log {
inline RED4ext::v1::PluginHandle g_handle{};
inline const RED4ext::v1::Logger *g_logger{};

// The log file of our own (started empty); false when it cannot be written.
bool OpenFile(const std::filesystem::path &path);
void CloseFile();
// A line to it, when there is one.
void ToFile(const char *level, const std::string &message);

template <class... Args> void Info(std::format_string<Args...> format, Args &&...args) {
  if (!g_logger)
    return;
  const auto message = std::format(format, std::forward<Args>(args)...);
  g_logger->Info(g_handle, message.c_str());
  ToFile("info", message);
}

template <class... Args> void Warn(std::format_string<Args...> format, Args &&...args) {
  if (!g_logger)
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
