#include "tandem/mods.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <regex>
#include <system_error>

namespace tandem::mods {
namespace {
std::string Lower(std::string_view text) {
  std::string out(text);
  for (auto &c : out)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return out;
}

bool Contains(std::string_view text, std::string_view part) {
  return text.find(part) != std::string_view::npos;
}

bool EndsWith(std::string_view text, std::string_view end) { return Lower(text).ends_with(end); }

// A path as bytes (UTF-8), whatever the platform's own encoding.
std::string Bytes(const std::filesystem::path &path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char *>(text.data()), text.size()};
}

// ---- Telling what an entry adds ----

// A class whose methods only draw: menus, the HUD, tooltips, popups.
bool IsInterface(std::string_view name) {
  for (const auto part : {"GameController", "LogicController", "Hud", "HUD", "Menu", "Tooltip",
                          "Popup", "UI", "Widget", "Notification", "LoadingScreen", "Settings"})
    if (Contains(name, part))
      return true;
  return name.starts_with("ink");
}

// Fields of a record that only change what is shown.
bool IsShownField(std::string_view field) {
  for (const auto shown : {"displayName", "localizedDescription", "localizedName", "iconPath",
                           "icon", "UIIcon", "description", "appearanceName", "uiData",
                           "displayNameKey", "atlasPartName", "atlasResourcePath"})
    if (field == shown)
      return true;
  return false;
}

std::uint8_t OfRecordName(std::string_view record) {
  if (record.starts_with("Items."))
    return kItems;
  if (record.starts_with("Vehicle."))
    return kVehicles;
  if (record.starts_with("Character."))
    return kNpcs;
  return kGameplay;
}

std::string Trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\"'");
  if (first == std::string_view::npos)
    return {};
  const auto last = text.find_last_not_of(" \t\r\"'");
  return std::string(text.substr(first, last - first + 1));
}

// A TweakXL file (YAML, or the .tweak source format): new records by where
// they live, changed fields by what they change.
std::uint8_t OfTweak(std::string_view text, bool source) {
  std::uint8_t out = 0;
  std::string top;        // the record (or record.field) the block is about
  std::size_t indent = 0; // its first child's indentation (0: none seen yet)
  bool topAdded = false;
  const auto changed = [&](const std::string &key) {
    // "Items.X.field": the record is all but the last part.
    const auto dot = key.rfind('.');
    const auto first = key.find('.');
    if (dot == std::string::npos || dot == first)
      return; // a record name alone: its fields say (below)
    out |= IsShownField(key.substr(dot + 1)) ? kLocal : kGameplay;
  };
  std::size_t start = 0;
  while (start <= text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string_view::npos)
      end = text.size();
    const auto line = text.substr(start, end - start);
    start = end + 1;
    const auto content = line.find_first_not_of(" \t");
    if (content == std::string_view::npos || line[content] == '#' ||
        line.substr(content).starts_with("//"))
      continue;
    if (content == 0) {
      // A top-level key: "Items.X:" (YAML) or "Items.X : Items.Base" (.tweak).
      const auto colon = line.find(':');
      const auto equals = line.find('=');
      if (source && equals != std::string_view::npos &&
          (colon == std::string_view::npos || equals < colon)) {
        changed(Trim(line.substr(0, equals)));
        top.clear();
        continue;
      }
      if (colon == std::string_view::npos)
        continue;
      top = Trim(line.substr(0, colon));
      indent = 0;
      topAdded = false;
      if (source && !Trim(line.substr(colon + 1)).empty()) {
        // ".tweak": a record with a base is a new one.
        out |= OfRecordName(top);
        topAdded = true;
      }
      changed(top);
      continue;
    }
    if (top.empty())
      continue;
    if (indent == 0)
      indent = content;
    if (content != indent)
      continue;
    const auto rest = line.substr(content);
    if (rest.starts_with("$type:") || rest.starts_with("$base:") ||
        rest.starts_with("$instances:")) {
      if (!topAdded)
        out |= OfRecordName(top);
      topAdded = true;
    } else if (!rest.starts_with("-")) {
      const auto colon = rest.find(':');
      if (colon != std::string_view::npos && !topAdded && top.find('.') == top.rfind('.'))
        out |= IsShownField(Trim(rest.substr(0, colon))) ? kLocal : kGameplay;
    }
  }
  return out ? out : kGameplay;
}

// Redscript: the classes it changes; none, a library others call.
std::uint8_t OfScript(std::string_view text) {
  static const std::regex hook(
      R"(@(wrapMethod|replaceMethod|addMethod|addField|wrapConstructor)\(\s*([A-Za-z0-9_.]+)\s*\))");
  std::uint8_t out = 0;
  const std::string all(text);
  for (std::sregex_iterator it(all.begin(), all.end(), hook), end; it != end; ++it)
    out |= IsInterface((*it)[2].str()) ? kLocal : kGameplay;
  if (Contains(text, "@replaceGlobal") || Contains(text, "@wrapGlobal"))
    out |= kGameplay;
  return out ? out : kLocal;
}

// Cyber Engine Tweaks (Lua): records made, flats set, methods overridden,
// the world's weather or clock changed; else it only draws or listens.
std::uint8_t OfCet(std::string_view text) {
  static const std::regex created(R"(TweakDB:(CreateRecord|CloneRecord)\(\s*["']([A-Za-z0-9_.]+))");
  static const std::regex overridden(R"(Override\(\s*["']([A-Za-z0-9_]+)["'])");
  std::uint8_t out = 0;
  const std::string all(text);
  for (std::sregex_iterator it(all.begin(), all.end(), created), end; it != end; ++it)
    out |= OfRecordName((*it)[2].str());
  for (std::sregex_iterator it(all.begin(), all.end(), overridden), end; it != end; ++it)
    out |= IsInterface((*it)[1].str()) ? kLocal : kGameplay;
  for (const auto part : {"TweakDB:SetFlat", "TweakDB:SetFlats", "TweakDB:Update", "SetWeather",
                          "SetGameTime", "SetTimeDilation"})
    if (Contains(text, part))
      out |= kGameplay;
  return out ? out : kLocal;
}

// ArchiveXL: new item appearances (factories), character creator options
// (customizations), patched appearance files; text and world streaming only show.
std::uint8_t OfArchiveXl(std::string_view text) {
  std::uint8_t out = 0;
  if (Contains(text, "\nfactories:") || text.starts_with("factories:"))
    out |= kItems;
  if (Contains(text, "customizations:"))
    out |= kLooks;
  if (Contains(text, ".app"))
    out |= kLooks;
  return out ? out : kLocal;
}

// The frameworks other mods are built on: libraries, nothing of their own.
bool IsFramework(std::string_view key) {
  for (const auto name :
       {"archivexl", "tweakxl", "codeware", "red4ext", "mod_settings", "input_loader",
        "redfilesystem", "reddata", "audioware", "nativesettings", "cybercmd"})
    if (key == name)
      return true;
  return false;
}

// The same from names alone: what each kind of file usually is.
std::uint8_t OfNames(Source source, std::string_view name, std::span<const File> files) {
  switch (source) {
  case Source::Red4ext:
    return IsFramework(Key(name)) ? kLocal : kUnknown;
  case Source::Cet:
    return IsFramework(Key(name)) ? kLocal : kUnknown;
  case Source::Script: {
    if (IsFramework(Key(name)))
      return kLocal;
    bool shows = !files.empty();
    for (const auto &file : files) {
      const auto stem = std::filesystem::path(file.path).stem().string();
      shows &= IsInterface(stem) || Contains(Lower(stem), "config");
    }
    return shows ? kLocal : kUnknown;
  }
  case Source::Tweak:
    return kGameplay;
  case Source::Archive:
    return kLocal;
  case Source::Redmod:
    return kUnknown;
  }
  return kUnknown;
}

bool Read(Source source, std::string_view file) {
  switch (source) {
  case Source::Script:
    return EndsWith(file, ".reds");
  case Source::Tweak:
    return EndsWith(file, ".yaml") || EndsWith(file, ".yml") || EndsWith(file, ".tweak");
  case Source::Archive:
    return EndsWith(file, ".xl");
  case Source::Cet:
    return EndsWith(file, ".lua");
  case Source::Redmod:
    return EndsWith(file, ".yaml") || EndsWith(file, ".yml") || EndsWith(file, ".tweak") ||
           EndsWith(file, ".script");
  case Source::Red4ext:
    break;
  }
  return false;
}

// Bounds on what a scan reads: a game with thousands of mods starts as fast.
constexpr std::uintmax_t kMaxFileBytes = 512 * 1024;
constexpr std::uintmax_t kMaxScanBytes = 32 * 1024 * 1024;
constexpr std::size_t kMaxFilesPerEntry = 4000;

struct Budget {
  std::uintmax_t left = kMaxScanBytes;
};

std::vector<File> FilesOf(const std::filesystem::path &entry, Source source, Budget &budget) {
  std::vector<File> files;
  std::error_code ec;
  const auto add = [&](const std::filesystem::path &path, const std::filesystem::path &base) {
    File file{.path = Bytes(path.lexically_relative(base))};
    std::error_code ignored;
    const auto size = std::filesystem::file_size(path, ignored);
    if (!ignored && Read(source, file.path) && size <= kMaxFileBytes && size <= budget.left) {
      std::ifstream in(path, std::ios::binary);
      file.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      budget.left -= size;
    }
    files.push_back(std::move(file));
  };
  if (std::filesystem::is_regular_file(entry, ec)) {
    add(entry, entry.parent_path());
    return files;
  }
  for (std::filesystem::recursive_directory_iterator it(entry, ec), end;
       !ec && it != end && files.size() < kMaxFilesPerEntry; it.increment(ec)) {
    std::error_code ignored;
    if (it->is_regular_file(ignored))
      add(it->path(), entry);
  }
  return files;
}

bool IsOurs(std::string_view name) {
  const auto lower = Lower(name);
  return lower == "tandem" || lower.starts_with("tandem.");
}
} // namespace

std::uint8_t Primary(std::uint8_t categories) {
  for (const auto category : kCategoryOrder)
    if (categories & category)
      return category;
  return kUnknown;
}

std::string_view CategoryName(std::uint8_t category) {
  switch (Primary(category)) {
  case kItems:
    return "items";
  case kVehicles:
    return "vehicles";
  case kNpcs:
    return "characters";
  case kLooks:
    return "looks";
  case kGameplay:
    return "gameplay";
  case kLocal:
    return "local";
  default:
    return "unknown";
  }
}

std::uint8_t Classify(Source source, std::string_view name, std::span<const File> files) {
  if (IsFramework(Key(name)))
    return kLocal;
  std::uint8_t out = 0;
  bool read = false;
  for (const auto &file : files) {
    if (file.text.empty())
      continue;
    read = true;
    const auto &text = file.text;
    switch (source) {
    case Source::Script:
      out |= OfScript(text);
      break;
    case Source::Tweak:
      out |= OfTweak(text, EndsWith(file.path, ".tweak"));
      break;
    case Source::Archive:
      out |= OfArchiveXl(text);
      break;
    case Source::Cet:
      out |= OfCet(text);
      break;
    case Source::Redmod:
      // REDmod: its tweaks as TweakXL's; its .script files replace the game's.
      out |=
          EndsWith(file.path, ".script") ? kGameplay : OfTweak(text, EndsWith(file.path, ".tweak"));
      break;
    case Source::Red4ext:
      break;
    }
  }
  if (source == Source::Redmod)
    for (const auto &file : files)
      if (Lower(file.path).starts_with("archives/"))
        out |= kLocal;
  return read && out ? out : static_cast<std::uint8_t>(out | OfNames(source, name, files));
}

std::string Key(std::string_view entry) {
  auto key = Lower(entry);
  for (bool again = true; again;) {
    again = false;
    for (const auto extension :
         {".archive", ".xl", ".reds", ".yaml", ".yml", ".tweak", ".lua", ".script"})
      if (key.ends_with(extension) && key.size() > std::string_view(extension).size()) {
        key.resize(key.size() - std::string_view(extension).size());
        again = true;
      }
  }
  const auto strip = [&] {
    const auto first = key.find_first_not_of("#!_~- ");
    key = first == std::string::npos ? std::string() : key.substr(first);
  };
  strip();
  // "zz-", "zzz_": a name pushed to the end of the load order.
  const auto zs = key.find_first_not_of('z');
  if (zs != std::string::npos && zs > 0 && zs + 1 < key.size() &&
      (key[zs] == '-' || key[zs] == '_' || key[zs] == ' '))
    key = key.substr(zs + 1);
  strip();
  while (!key.empty() && key.back() == ' ')
    key.pop_back();
  return key;
}

std::vector<Mod> Group(std::span<const Entry> entries) {
  std::map<std::string, Mod> byKey;
  for (const auto &entry : entries) {
    const auto key = Key(entry.name);
    if (key.empty())
      continue;
    auto &mod = byKey[key];
    if (mod.name.empty()) {
      auto name = entry.name;
      for (const auto extension : {".archive.xl", ".archive", ".xl", ".reds", ".yaml", ".yml",
                                   ".tweak", ".lua", ".script"})
        if (EndsWith(name, extension) && name.size() > std::string_view(extension).size()) {
          name.resize(name.size() - std::string_view(extension).size());
          break;
        }
      mod.name = name.substr(0, proto::kMaxModName);
      mod.hash = NameHash(key);
    }
    mod.categories |= entry.categories;
  }
  std::vector<Mod> out;
  for (auto &[key, mod] : byKey)
    out.push_back(std::move(mod));
  std::ranges::sort(out, {}, &Mod::hash);
  if (out.size() > proto::kMaxModEntries)
    out.resize(proto::kMaxModEntries);
  return out;
}

std::vector<Entry> ScanEntries(const std::filesystem::path &game) {
  std::vector<Entry> entries;
  Budget budget;
  const auto list = [&](const std::filesystem::path &folder, Source source, bool folders,
                        std::span<const std::string_view> extensions) {
    std::error_code ec;
    std::vector<std::filesystem::path> paths;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end;
         it.increment(ec))
      paths.push_back(it->path());
    std::ranges::sort(paths);
    for (const auto &path : paths) {
      std::error_code ignored;
      const bool directory = std::filesystem::is_directory(path, ignored);
      const auto name = Bytes(path.filename());
      if (IsOurs(name) || (folders && !directory))
        continue;
      if (!directory && !extensions.empty() &&
          std::ranges::none_of(extensions, [&](auto e) { return EndsWith(name, e); }))
        continue;
      const auto files = FilesOf(path, source, budget);
      entries.push_back({source, name, Classify(source, name, files)});
    }
  };
  constexpr std::string_view any[] = {""};
  constexpr std::string_view scripts[] = {".reds"};
  constexpr std::string_view tweaks[] = {".yaml", ".yml", ".tweak"};
  constexpr std::string_view archives[] = {".archive", ".xl"};
  list(game / "red4ext" / "plugins", Source::Red4ext, true, any);
  list(game / "bin" / "x64" / "plugins" / "cyber_engine_tweaks" / "mods", Source::Cet, true, any);
  list(game / "r6" / "scripts", Source::Script, false, scripts);
  list(game / "r6" / "tweaks", Source::Tweak, false, tweaks);
  list(game / "archive" / "pc" / "mod", Source::Archive, false, archives);
  list(game / "mods", Source::Redmod, true, any);
  return entries;
}

std::vector<Mod> Scan(const std::filesystem::path &game) {
  const auto entries = ScanEntries(game);
  return Group(entries);
}

std::vector<Entry> EntriesOfList(std::string_view list) {
  constexpr std::pair<std::string_view, Source> sources[] = {
      {"red4ext", Source::Red4ext}, {"cet", Source::Cet},         {"script", Source::Script},
      {"tweak", Source::Tweak},     {"archive", Source::Archive}, {"redmod", Source::Redmod}};
  // In the order they come, each entry with its files.
  std::vector<std::pair<std::pair<Source, std::string>, std::vector<File>>> found;
  std::size_t start = 0;
  while (start < list.size()) {
    auto end = list.find('\n', start);
    if (end == std::string_view::npos)
      end = list.size();
    auto line = list.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (line.empty() || line.front() == '#')
      continue;
    const auto tab = line.find('\t');
    if (tab == std::string_view::npos)
      continue;
    const auto rest = line.substr(tab + 1);
    const auto second = rest.find('\t');
    const auto entry = std::string(rest.substr(0, second));
    const auto file =
        second == std::string_view::npos ? std::string() : std::string(rest.substr(second + 1));
    const auto source = std::ranges::find(sources, line.substr(0, tab),
                                          &std::pair<std::string_view, Source>::first);
    if (source == std::end(sources) || entry.empty() || IsOurs(entry))
      continue;
    const std::pair key{source->second, entry};
    auto at = std::ranges::find(found, key, &decltype(found)::value_type::first);
    if (at == found.end()) {
      found.push_back({key, {}});
      at = found.end() - 1;
    }
    if (!file.empty())
      at->second.push_back({file, {}});
  }
  std::vector<Entry> out;
  for (const auto &[key, files] : found)
    out.push_back({key.first, key.second, Classify(key.first, key.second, files)});
  return out;
}

std::vector<proto::ModEntry> ToWire(std::span<const Mod> mods, bool names) {
  std::vector<proto::ModEntry> out;
  for (const auto &mod : mods)
    out.push_back({mod.hash, mod.categories, names ? mod.name : std::string()});
  return out;
}

Comparison Compare(std::span<const proto::ModEntry> ours, std::span<const proto::ModEntry> theirs) {
  const auto has = [](std::span<const proto::ModEntry> set, std::uint64_t hash) {
    return std::ranges::any_of(set, [&](const auto &mod) { return mod.hash == hash; });
  };
  const auto count = [](Counts &counts, std::uint8_t categories) {
    ++counts.total;
    const auto primary = Primary(categories);
    for (std::size_t i = 0; i < kCategoryOrder.size(); ++i)
      if (kCategoryOrder[i] == primary)
        ++counts.by[i];
  };
  Comparison out;
  for (const auto &mod : theirs) {
    if (has(ours, mod.hash)) {
      ++out.shared;
      continue;
    }
    count(out.theirsOnly, mod.categories);
    out.theirsOnlyHashes.push_back(mod.hash);
  }
  for (const auto &mod : ours)
    if (!has(theirs, mod.hash))
      count(out.oursOnly, mod.categories);
  return out;
}

std::string Describe(const Comparison &comparison, std::string_view who) {
  const auto parts = [](const Counts &counts) {
    std::string out;
    for (std::size_t i = 0; i < kCategoryOrder.size(); ++i) {
      if (!counts.by[i])
        continue;
      out += std::format("{}{} {}", out.empty() ? "" : ", ", counts.by[i],
                         CategoryName(kCategoryOrder[i]));
    }
    return out;
  };
  std::string out;
  if (comparison.theirsOnly.total)
    out = std::format("{} has mods you don't ({}): {}", who, comparison.theirsOnly.total,
                      parts(comparison.theirsOnly));
  if (comparison.oursOnly.total)
    out += std::format("{}you have mods {} doesn't ({}): {}", out.empty() ? "" : "; ", who,
                       comparison.oursOnly.total, parts(comparison.oursOnly));
  return out;
}

// ---- Records from another game ----

std::string_view KindName(Kind kind) {
  constexpr std::string_view names[] = {"weapon",  "clothing",  "look",
                                        "vehicle", "character", "character-look",
                                        "item",    "effect",    "radio"};
  static_assert(std::size(names) == kKindCount);
  const auto index = static_cast<std::size_t>(kind);
  return index < std::size(names) ? names[index] : "record";
}

namespace {
// A vanilla record tandem falls back on (the names are the game's; each is
// checked to exist in the game before it is used: research §13.55).
struct Vanilla {
  std::string_view name;
  std::uint64_t id;
  constexpr Vanilla(std::string_view n) : name(n), id(RecordId(n)) {}
};

// By the hint's type (an enum name), a stand-in of the same type.
struct ByType {
  std::string_view type;
  Vanilla record;
};

constexpr Vanilla kHandgun{"Items.Preset_Lexington_Default"};
constexpr Vanilla kGrenade{"Items.GrenadeFragRegular"};
constexpr ByType kWeapons[] = {
    {"Wea_Handgun", kHandgun},
    {"Wea_Revolver", {"Items.Preset_Overture_Default"}},
    {"Wea_SubmachineGun", {"Items.Preset_Saratoga_Default"}},
    {"Wea_AssaultRifle", {"Items.Preset_Ajax_Default"}},
    {"Wea_Rifle", {"Items.Preset_Ajax_Default"}},
    {"Wea_PrecisionRifle", {"Items.Preset_Achilles_Default"}},
    {"Wea_SniperRifle", {"Items.Preset_Grad_Default"}},
    {"Wea_Shotgun", {"Items.Preset_Carnage_Default"}},
    {"Wea_ShotgunDual", {"Items.Preset_Igla_Default"}},
    {"Wea_LightMachineGun", {"Items.Preset_Defender_Default"}},
    {"Wea_HeavyMachineGun", {"Items.Preset_Defender_Default"}},
    {"Wea_Katana", {"Items.Preset_Katana_Default"}},
    {"Wea_Sword", {"Items.Preset_Katana_Default"}},
    {"Wea_LongBlade", {"Items.Preset_Katana_Default"}},
    {"Wea_Knife", {"Items.Preset_Knife_Default"}},
    {"Wea_ShortBlade", {"Items.Preset_Knife_Default"}},
    {"Wea_Machete", {"Items.Preset_Machete_Default"}},
    {"Wea_Hammer", {"Items.Preset_Hammer_Default"}},
    {"Wea_OneHandedClub", {"Items.Preset_Baseball_Bat_Default"}},
    {"Wea_TwoHandedClub", {"Items.Preset_Baseball_Bat_Default"}},
    {"Gad_Grenade", kGrenade},
};
// By the item's category when its type has no stand-in.
constexpr ByType kWeaponCategories[] = {{"Weapon", kHandgun}, {"Gadget", kGrenade}};

// Clothes: the plain piece of the same slot; a hat, glasses, a jacket or a
// whole outfit is left off rather than replaced by another.
constexpr ByType kClothes[] = {
    {"Clo_InnerChest", {"Items.FormalShirt_02_basic_03"}},
    {"Clo_Legs", {"Items.Pants_02_basic_01"}},
    {"Clo_Feet", {"Items.CasualShoes_01_basic_01"}},
};

constexpr Vanilla kCar{"Vehicle.v_standard2_archer_hella_player"};
constexpr Vanilla kSportsCar{"Vehicle.v_sport2_porsche_911turbo"};
constexpr Vanilla kVan{"Vehicle.v_standard25_mahir_supron_player"};
constexpr Vanilla kBike{"Vehicle.v_sportbike2_arch_player"};
constexpr Vanilla kAv{"Vehicle.max_tac_av"};

constexpr Vanilla kMan{"Character.kab_tyger_claws_biker1_ranged1_saratoga_ma"};
constexpr Vanilla kWoman{"Character.rcr_sixthstreet_hooligan_melee1_knife_wa"};

const Vanilla *Find(std::span<const ByType> table, std::uint64_t hash) {
  for (const auto &entry : table)
    if (NameHash(entry.type) == hash)
      return &entry.record;
  return nullptr;
}

void Push(std::vector<Candidate> &out, const Vanilla &record, Outcome outcome) {
  if (std::ranges::none_of(out, [&](const auto &c) { return c.record == record.id; }))
    out.push_back({record.id, record.name, outcome});
}
} // namespace

std::vector<Candidate> Candidates(Kind kind, const proto::RecordHint &hint) {
  std::vector<Candidate> out;
  switch (kind) {
  case Kind::Weapon:
    if (const auto *same = Find(kWeapons, hint.type))
      Push(out, *same, Outcome::Similar);
    if (const auto *same = Find(kWeaponCategories, hint.tag))
      Push(out, *same, Outcome::Similar);
    Push(out, kHandgun, Outcome::Default);
    break;
  case Kind::Clothing:
    if (const auto *same = Find(kClothes, hint.type))
      Push(out, *same, Outcome::Similar);
    break;
  case Kind::Vehicle:
    if (hint.type == NameHash("Bike")) {
      Push(out, kBike, Outcome::Similar);
    } else if (hint.type == NameHash("AV")) {
      Push(out, kAv, Outcome::Similar);
    } else if (hint.type == NameHash("Car")) {
      Push(out,
           hint.size != 0 && hint.size <= 2 ? kSportsCar
           : hint.size >= 5                 ? kVan
                                            : kCar,
           Outcome::Similar);
    }
    Push(out, kCar, Outcome::Default);
    break;
  case Kind::Npc:
    if (hint.type == NameHash("Human"))
      Push(out, hint.tag == NameHash("Female") ? kWoman : kMan, Outcome::Similar);
    Push(out, hint.tag == NameHash("Female") ? kWoman : kMan, Outcome::Default);
    break;
  case Kind::LookPiece:
  case Kind::NpcLook:
  case Kind::Item:
  case Kind::Effect:
  case Kind::Radio:
    break;
  }
  return out;
}

std::string_view NameOf(std::uint64_t record) {
  static const auto all = [] {
    std::vector<Candidate> out;
    for (std::size_t kind = 0; kind < kKindCount; ++kind)
      for (const auto &hint :
           {proto::RecordHint{}, proto::RecordHint{.size = 2}, proto::RecordHint{.size = 6}})
        for (const auto &c : Candidates(static_cast<Kind>(kind), hint))
          out.push_back(c);
    for (const auto table : {std::span<const ByType>(kWeapons), std::span<const ByType>(kClothes)})
      for (const auto &entry : table)
        out.push_back({entry.record.id, entry.record.name, Outcome::Similar});
    for (const auto &record : {kSportsCar, kVan, kBike, kAv, kWoman})
      out.push_back({record.id, record.name, Outcome::Similar});
    return out;
  }();
  for (const auto &c : all)
    if (c.record == record)
      return c.name;
  return {};
}

Resolved Resolve(Kind kind, std::uint64_t record, const proto::RecordHint &hint,
                 const Known &known) {
  if (record == 0 || !known || known(kind, record))
    return {record, Outcome::Same};
  for (const auto &candidate : Candidates(kind, hint))
    if (known(kind, candidate.record))
      return {candidate.record, candidate.outcome};
  return {0, Outcome::Skipped};
}

Resolved Resolver::Resolve(Kind kind, std::uint64_t record, const proto::RecordHint &hint,
                           proto::PlayerId from, std::string_view who, std::uint64_t nowUs) {
  if (record == 0)
    return {};
  const MemoKey key{kind, record, hint.type, hint.size};
  auto found = m_memo.find(key);
  if (found == m_memo.end())
    found = m_memo.emplace(key, mods::Resolve(kind, record, hint, m_known)).first;
  if (found->second.outcome != Outcome::Same)
    Add(kind, record, found->second, from, who, nowUs);
  return found->second;
}

void Resolver::Note(Kind kind, std::uint64_t record, Resolved to, proto::PlayerId from,
                    std::string_view who, std::uint64_t nowUs) {
  if (to.outcome != Outcome::Same)
    Add(kind, record, to, from, who, nowUs);
}

void Resolver::Add(Kind kind, std::uint64_t record, Resolved to, proto::PlayerId from,
                   std::string_view who, std::uint64_t nowUs) {
  if (!m_seen.emplace(std::tuple{from, kind, record}, true).second)
    return;
  m_list.push_back({from, std::string(who), kind, record, to});
  const auto shown = to.record == 0 ? std::string(kind == Kind::NpcLook ? "its usual look"
                                                  : kind == Kind::Radio ? "the radio off"
                                                  : kind == Kind::Item  ? "nothing: refused"
                                                                        : "nothing")
                     : NameOf(to.record).empty() ? std::format("{:#018x}", to.record)
                                                 : std::string(NameOf(to.record));
  m_log.push_back(std::format("mods: unknown {} {:#018x} from {}, shown as {}", KindName(kind),
                              record, who, shown));
  auto [pending, added] = m_pending.try_emplace({from, kind});
  if (added)
    pending->second = {std::string(who), nowUs, 0, to.outcome, false};
  ++pending->second.count;
  auto &told = m_told[from];
  told.owner = from;
  told.kinds |= static_cast<std::uint16_t>(1u << static_cast<unsigned>(kind));
  ++told.count;
}

std::vector<std::string> Resolver::TakeLog() { return std::exchange(m_log, {}); }

std::vector<Notice> Resolver::TakeNotices(std::uint64_t nowUs) {
  std::vector<Notice> out;
  for (auto &[key, pending] : m_pending)
    if (!pending.told && nowUs - pending.sinceUs >= kGatherUs) {
      pending.told = true;
      out.push_back({key.first, pending.who, key.second, pending.count, pending.outcome});
    }
  return out;
}

std::vector<proto::Substitutes> Resolver::TakeTold() {
  std::vector<proto::Substitutes> out;
  for (auto &[player, told] : m_told) {
    // Only when a kind is new: the counts alone are not news.
    auto &sent = m_toldSent[player];
    if ((told.kinds & ~sent) == 0)
      continue;
    sent = told.kinds;
    out.push_back(told);
  }
  return out;
}

void Resolver::Clear() {
  m_memo.clear();
  m_seen.clear();
  m_pending.clear();
  m_told.clear();
  m_toldSent.clear();
  m_list.clear();
  m_log.clear();
}
} // namespace tandem::mods
