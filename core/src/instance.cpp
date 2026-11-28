#include "tandem/instance.hpp"

#include <charconv>

namespace tandem::instance {
namespace {
std::filesystem::path From(const std::filesystem::path &directory,
                           const std::filesystem::path &path) {
  // operator/ keeps an absolute `path` as it is.
  return (directory / path).lexically_normal();
}
} // namespace

Files Resolve(const std::filesystem::path &pluginDirectory, const Overrides &overrides) {
  Files files;
  files.config = overrides.config.empty() ? pluginDirectory / "tandem.ini"
                                          : From(pluginDirectory, overrides.config);
  files.command = overrides.command.empty() ? pluginDirectory / "tandem.cmd"
                                            : From(pluginDirectory, overrides.command);
  files.recent = files.config.parent_path() / "recent.txt";
  files.campaigns = files.config.parent_path() / "campaigns.txt";
  if (!overrides.log.empty())
    files.log = From(pluginDirectory, overrides.log);
  return files;
}

std::optional<std::uint16_t> ParsePort(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return std::nullopt;
  text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
  std::uint16_t port{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
  if (error != std::errc{} || end != text.data() + text.size() || port == 0)
    return std::nullopt;
  return port;
}
} // namespace tandem::instance
