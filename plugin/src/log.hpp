#pragma once

// RED4ext's logger (red4ext/logs/tandem-*.log), usable from any thread.

#include <RED4ext/Api/v1/Logger.hpp>
#include <RED4ext/Api/v1/PluginHandle.hpp>

#include <format>
#include <string>

namespace tandem::plugin::log {
inline RED4ext::v1::PluginHandle g_handle{};
inline const RED4ext::v1::Logger *g_logger{};

template <class... Args> void Info(std::format_string<Args...> format, Args &&...args) {
  if (g_logger)
    g_logger->Info(g_handle, std::format(format, std::forward<Args>(args)...).c_str());
}

template <class... Args> void Warn(std::format_string<Args...> format, Args &&...args) {
  if (g_logger)
    g_logger->Warn(g_handle, std::format(format, std::forward<Args>(args)...).c_str());
}

template <class... Args> void Error(std::format_string<Args...> format, Args &&...args) {
  if (g_logger)
    g_logger->Error(g_handle, std::format(format, std::forward<Args>(args)...).c_str());
}
} // namespace tandem::plugin::log
