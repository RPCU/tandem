#include "log.hpp"

#include <chrono>
#include <fstream>
#include <mutex>

namespace tandem::plugin::log {
namespace {
std::mutex g_fileMutex;
std::ofstream g_file;
} // namespace

bool OpenFile(const std::filesystem::path &path) {
  const std::scoped_lock lock(g_fileMutex);
  g_file.close();
  g_file.clear();
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  g_file.open(path, std::ios::trunc);
  return g_file.is_open();
}

void CloseFile() {
  const std::scoped_lock lock(g_fileMutex);
  g_file.close();
}

void ToFile(const char *level, const std::string &message) {
  const std::scoped_lock lock(g_fileMutex);
  if (!g_file.is_open())
    return;
  const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
  // One line at a time, flushed: the tools follow this file.
  g_file << std::format("[{:%F %T}] [{}] {}\n", now, level, message) << std::flush;
}
} // namespace tandem::plugin::log
