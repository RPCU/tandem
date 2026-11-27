#include "tandem/sim/looks.hpp"

#include "tandem/sim/names.hpp"

#include <cstdlib>
#include <exception>
#include <format>
#include <fstream>
#include <random>
#include <string_view>

namespace tandem::sim {
std::filesystem::path LookKeysDir(const std::string &option) {
  if (!option.empty())
    return option;
  if (const char *cache = std::getenv("XDG_CACHE_HOME"); cache && *cache)
    return std::filesystem::path(cache) / "tandem" / "looks";
  if (const char *home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".cache" / "tandem" / "looks";
  return "looks";
}

std::filesystem::path LookKeysFile(const std::filesystem::path &dir, proto::Body body) {
  return dir / (body == proto::Body::Male ? "male.keys" : "female.keys");
}

std::vector<proto::AppearanceKey> LoadLookKeys(const std::filesystem::path &file) {
  std::vector<proto::AppearanceKey> keys;
  std::ifstream in(file);
  for (std::string resource, definition; in >> resource >> definition;) {
    try {
      keys.push_back({std::stoull(resource, nullptr, 16), std::stoull(definition, nullptr, 16)});
    } catch (const std::exception &) {
      return {};
    }
  }
  if (keys.size() > proto::kMaxAppearanceKeys)
    keys.resize(proto::kMaxAppearanceKeys);
  return keys;
}

std::optional<proto::Body> SavedBody(const std::filesystem::path &dir) {
  for (const auto body : {proto::Body::Male, proto::Body::Female})
    if (!LoadLookKeys(LookKeysFile(dir, body)).empty())
      return body;
  return std::nullopt;
}

proto::Appearance RandomLook(proto::Body body, unsigned seed,
                             std::vector<proto::AppearanceKey> keys,
                             const std::vector<std::pair<std::string, proto::RecordHint>> &extra,
                             std::string *picked) {
  static const std::vector<std::vector<std::string_view>> slots = {
      {"Items.FormalShirt_01_rich_06", "Items.FormalShirt_02_basic_03",
       "Items.LooseShirt_01_rich_02", "Items.Fixer_01_Set_TShirt"},
      {"Items.Jacket_19_basic_04", "Items.Jacket_20_basic_01", "Items.Jacket_01_rich_01",
       "Items.Jacket_17_rich_06", "Items.Coat_01_basic_03", "Items.Coat_04_rich_02",
       "Items.FormalJacket_02_basic_02", "Items.FormalJacket_04_basic_02"},
      {"Items.Pants_02_basic_01", "Items.Pants_04_basic_01", "Items.Pants_08_rich_01",
       "Items.FormalPants_02_rich_01", "Items.FormalPants_03_rich_02",
       "Items.FormalSkirt_01_rich_01", "Items.FormalSkirt_02_rich_03"},
      {"Items.Boots_03_basic_02", "Items.Boots_07_basic_01", "Items.Boots_09_rich_01",
       "Items.CasualShoes_01_basic_01", "Items.CasualShoes_02_basic_01",
       "Items.CasualShoes_05_basic_03", "Items.FormalShoes_01_rich_02",
       "Items.FormalShoes_03_rich_01"},
      // Head and face: sometimes nothing.
      {"", "", "Items.Cap_02_basic_01", "Items.Cap_03_rich_02", "Items.Hat_02_rich_02",
       "Items.Hat_03_basic_02"},
      {"", "", "Items.Glasses_01_basic_03", "Items.Glasses_03_basic_06"},
  };
  std::mt19937 random(seed);
  proto::Appearance look{.body = body, .look = std::move(keys)};
  std::string names;
  for (const auto &slot : slots) {
    const auto name = slot[random() % slot.size()];
    if (name.empty())
      continue;
    look.items.push_back(TweakDBID(name));
    look.hints.push_back({});
    names += std::format(" {}", name.substr(6));
  }
  for (const auto &[name, hint] : extra) {
    look.items.push_back(TweakDBID(name));
    look.hints.push_back(hint);
    names += std::format(" {} (a mod's)", name);
  }
  if (picked)
    *picked = std::move(names);
  return look;
}
} // namespace tandem::sim
