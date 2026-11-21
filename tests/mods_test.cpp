// Other mods (docs/architecture/mod-compat.md): what a mod adds, from its
// files; two games' sets compared; the one resolver for records another
// game names, and that every replacement it makes is told.

#include "ports.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/mods.hpp"
#include "tandem/sim/names.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

#include <unistd.h>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
struct Scratch {
  std::filesystem::path root;
  explicit Scratch(const char *name) {
    root = std::filesystem::temp_directory_path() / std::format("tandem-{}-{}", name, getpid());
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
  }
  ~Scratch() { std::filesystem::remove_all(root); }
  void Put(const std::string &file, std::string_view text = "") const {
    const auto path = root / file;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
  }
};

std::uint8_t Of(mods::Source source, std::string_view name, std::string_view path,
                std::string_view text = "") {
  const mods::File files[] = {{std::string(path), std::string(text)}};
  return mods::Classify(source, name, files);
}

const mods::Mod *Named(const std::vector<mods::Mod> &all, std::string_view key) {
  for (const auto &mod : all)
    if (mod.hash == mods::NameHash(key))
      return &mod;
  return nullptr;
}

constexpr auto kHandgun = mods::RecordId("Items.Preset_Lexington_Default");
constexpr auto kKatana = mods::RecordId("Items.Preset_Katana_Default");
constexpr auto kModdedGun = mods::RecordId("Items.Modded_Gun_Of_Doom");
} // namespace

TEST_CASE("names are hashed the game's way") {
  // TweakDBID: CRC32 and the length above it (the sim's, the peer's).
  CHECK(mods::RecordId("Items.money") == sim::TweakDBID("Items.money"));
  CHECK((mods::RecordId("Items.money") >> 32) == 11);
  CHECK(mods::NameHash("Car") == sim::CName("Car"));
  CHECK(mods::NameHash("") == 0xCBF29CE484222325ull);
  CHECK(mods::NameHash("Car") != mods::NameHash("car"));
}

TEST_CASE("a mod is sorted by what its files add") {
  using mods::Source;
  // Archives replace what exists; ArchiveXL adds item appearances or looks.
  CHECK(Of(Source::Archive, "ZZ-Hair2K.archive", "ZZ-Hair2K.archive") == mods::kLocal);
  CHECK(Of(Source::Archive, "New Jackets.xl", "New Jackets.xl", "factories:\n  - x.csv\n") ==
        mods::kItems);
  CHECK(Of(Source::Archive, "Hair.xl", "Hair.xl", "customizations:\n  male: a.inkcc\n") ==
        mods::kLooks);
  CHECK(Of(Source::Archive, "Lights.xl", "Lights.xl", "streaming:\n  sectors: []\n") ==
        mods::kLocal);
  // Tweaks: new records by where they live, changed fields by what they change.
  CHECK(Of(Source::Tweak, "guns", "guns.yaml",
           "Items.My_Gun:\n  $base: Items.Preset_Lexington_Default\n") == mods::kItems);
  CHECK(Of(Source::Tweak, "cars", "cars.yaml",
           "Vehicle.my_car:\n  $type: gamedataVehicle_Record\n") == mods::kVehicles);
  CHECK(Of(Source::Tweak, "npcs", "npcs.yaml", "Character.my_merc:\n  $base: Character.Panam\n") ==
        mods::kNpcs);
  CHECK(Of(Source::Tweak, "stats", "stats.yaml",
           "Items.Preset_Lexington_Default.statModifiers:\n  - $type: ConstantStatModifier\n") ==
        mods::kGameplay);
  CHECK(Of(Source::Tweak, "names", "names.yaml",
           "Items.Preset_Lexington_Default:\n  displayName: LocKey#1\n") == mods::kLocal);
  CHECK(Of(Source::Tweak, "src", "a.tweak", "Items.Mine : Items.Preset_Katana_Default\n{\n}\n") ==
        mods::kItems);
  // Scripts: the classes they change; none at all, a library.
  CHECK(Of(Source::Script, "Hud", "Hud.reds",
           "@wrapMethod(healthbarWidgetGameController)\nfunc OnInitialize() {}\n") == mods::kLocal);
  CHECK(Of(Source::Script, "Hacks", "Hacks.reds", "@replaceMethod(NPCPuppet)\nfunc X() {}\n") ==
        mods::kGameplay);
  CHECK(Of(Source::Script, "Lib", "Lib.reds", "public class Lib { }\n") == mods::kLocal);
  // CET: flats, overrides and the world's weather are gameplay; drawing is not.
  CHECK(Of(Source::Cet, "Weather", "init.lua", "Game.GetWeatherSystem():SetWeather(w, 5, 9)") ==
        mods::kGameplay);
  CHECK(Of(Source::Cet, "Blood", "init.lua", "Observe('NPCPuppet', 'OnHit', function() end)") ==
        mods::kLocal);
  CHECK(Of(Source::Cet, "Bikes", "init.lua", "TweakDB:CloneRecord('Vehicle.new_bike', 'x')") ==
        mods::kVehicles);
  // A DLL says nothing; the frameworks are known.
  CHECK(Of(Source::Red4ext, "SomePlugin", "SomePlugin.dll") == mods::kUnknown);
  CHECK(Of(Source::Red4ext, "ArchiveXL", "ArchiveXL.dll") == mods::kLocal);
}

TEST_CASE("the entries of one mod are one mod, compared by name in every game") {
  CHECK(mods::Key("Quickhack_Fixes.archive") == "quickhack_fixes");
  CHECK(mods::Key("Quickhack_Fixes.archive.xl") == "quickhack_fixes");
  CHECK(mods::Key("###-PreemMap.archive") == "preemmap");
  CHECK(mods::Key("zz-NPCs-Judy.archive") == "npcs-judy");
  CHECK(mods::Key("ZZ-Clothing.archive") == "clothing");
  CHECK(mods::Key("##GoreRunners") == "gorerunners");
  CHECK(mods::Key("Preem Water 2.0 - Pure.archive") == "preem water 2.0 - pure");
  const mods::Entry entries[] = {
      {mods::Source::Archive, "Quickhack_Fixes.archive", mods::kLocal},
      {mods::Source::Archive, "Quickhack_Fixes.archive.xl", mods::kLocal},
      {mods::Source::Script, "Quickhack_Fixes", mods::kGameplay},
      {mods::Source::Tweak, "Quickhack_Fixes", mods::kGameplay | mods::kItems},
      {mods::Source::Archive, "ZZ-Hair2K.archive", mods::kLocal},
  };
  const auto all = mods::Group(entries);
  REQUIRE(all.size() == 2);
  const auto *fixes = Named(all, "quickhack_fixes");
  REQUIRE(fixes);
  CHECK(fixes->name == "Quickhack_Fixes");
  CHECK(fixes->categories == (mods::kLocal | mods::kGameplay | mods::kItems));
  CHECK(mods::Primary(fixes->categories) == mods::kItems);
}

TEST_CASE("a game folder's mods are read, tandem's own left out") {
  const Scratch game("mods-scan");
  game.Put("red4ext/plugins/tandem/tandem.dll");
  game.Put("red4ext/plugins/Codeware/Codeware.dll");
  game.Put("red4ext/plugins/Mystery/Mystery.dll");
  game.Put("r6/scripts/tandem/System.reds", "@wrapMethod(NPCPuppet)\n");
  game.Put("r6/scripts/BetterHud/Hud.reds", "@wrapMethod(inkGameController)\n");
  game.Put("r6/tweaks/MoreGuns/guns.yaml",
           "Items.Gun_Two:\n  $base: Items.Preset_Katana_Default\n");
  game.Put("archive/pc/mod/MoreGuns.archive");
  game.Put("archive/pc/mod/MoreGuns.archive.xl", "factories:\n  - guns.csv\n");
  game.Put("archive/pc/mod/readme.txt");
  game.Put("bin/x64/plugins/cyber_engine_tweaks/mods/Sky/init.lua", "x:SetWeather(a, 1, 9)");
  const auto all = mods::Scan(game.root);
  CHECK(all.size() == 5);
  CHECK_FALSE(Named(all, "tandem"));
  REQUIRE(Named(all, "moreguns"));
  CHECK(Named(all, "moreguns")->categories == (mods::kItems | mods::kLocal));
  CHECK(Named(all, "betterhud")->categories == mods::kLocal);
  CHECK(Named(all, "sky")->categories == mods::kGameplay);
  CHECK(Named(all, "mystery")->categories == mods::kUnknown);
  CHECK(Named(all, "codeware")->categories == mods::kLocal);
  CHECK(mods::Scan(game.root / "nowhere").empty());
}

TEST_CASE("a real install, by its file names alone (a sample, without contents)") {
  std::ifstream file(std::filesystem::path(TANDEM_TEST_DATA) / "mods-sample.tsv");
  REQUIRE(file);
  std::stringstream text;
  text << file.rdbuf();
  const auto entries = mods::EntriesOfList(text.str());
  const auto all = mods::Group(entries);
  // 12 plugins, 8 CET mods, 15 script folders, 3 tweak folders, 88 archives,
  // grouped by name: a mod's archive, .xl, scripts and tweaks are one.
  CHECK(entries.size() == 12 + 8 + 15 + 3 + 88);
  CHECK(all.size() < entries.size());
  const auto *fixes = Named(all, "quickhack_fixes");
  REQUIRE(fixes);
  CHECK((fixes->categories & mods::kGameplay) != 0);
  const auto *judy = Named(all, "npcs-judy");
  REQUIRE(judy);
  CHECK(judy->categories == mods::kLocal);
  CHECK(Named(all, "archivexl")->categories == mods::kLocal);
  CHECK(Named(all, "torrent")->categories == mods::kUnknown);
  CHECK(Named(all, "loadbegone"));
  // Every mod falls somewhere.
  for (const auto &mod : all)
    CHECK(mod.categories != 0);
}

TEST_CASE("two games' mods are compared by name, counted by what they add") {
  const std::vector<proto::ModEntry> ours = {{1, mods::kLocal}, {2, mods::kGameplay}};
  const std::vector<proto::ModEntry> theirs = {{1, mods::kLocal},
                                               {3, mods::kItems | mods::kGameplay},
                                               {4, mods::kItems},
                                               {5, mods::kGameplay}};
  const auto comparison = mods::Compare(ours, theirs);
  CHECK(comparison.shared == 1);
  CHECK(comparison.theirsOnly.total == 3);
  CHECK(comparison.theirsOnly.by[0] == 2); // items
  CHECK(comparison.theirsOnly.by[4] == 1); // gameplay
  CHECK(comparison.oursOnly.total == 1);
  CHECK(comparison.theirsOnlyHashes == std::vector<std::uint64_t>{3, 4, 5});
  CHECK(mods::Describe(comparison, "Judy") ==
        "Judy has mods you don't (3): 2 items, 1 gameplay; you have mods Judy doesn't (1): 1 "
        "gameplay");
  CHECK_FALSE(mods::Compare(ours, ours).Differ());
}

TEST_CASE("a record this game has is itself; one it lacks, a vanilla one like it") {
  std::set<std::uint64_t> here = {kHandgun, kKatana};
  const mods::Known known = [&](mods::Kind, std::uint64_t record) { return here.contains(record); };
  using mods::Kind, mods::Outcome;
  const auto katana =
      proto::RecordHint{.type = mods::NameHash("Wea_Katana"), .tag = mods::NameHash("Weapon")};
  CHECK(mods::Resolve(Kind::Weapon, kKatana, katana, known) ==
        mods::Resolved{kKatana, Outcome::Same});
  CHECK(mods::Resolve(Kind::Weapon, kModdedGun, katana, known) ==
        mods::Resolved{kKatana, Outcome::Similar});
  // A type with no stand-in here: its category's, then the kind's default.
  const auto revolver =
      proto::RecordHint{.type = mods::NameHash("Wea_Revolver"), .tag = mods::NameHash("Weapon")};
  CHECK(mods::Resolve(Kind::Weapon, kModdedGun, revolver, known) ==
        mods::Resolved{kHandgun, Outcome::Similar});
  CHECK(mods::Resolve(Kind::Weapon, kModdedGun, {}, known) ==
        mods::Resolved{kHandgun, Outcome::Default});
  // Kinds with no stand-in: nothing (a traded or dropped item is refused).
  CHECK(mods::Resolve(Kind::Item, kModdedGun, katana, known) ==
        mods::Resolved{0, Outcome::Skipped});
  // Vehicles by type and seats; NPCs by type and gender.
  here.insert(mods::RecordId("Vehicle.v_sportbike2_arch_player"));
  here.insert(mods::RecordId("Vehicle.v_standard2_archer_hella_player"));
  here.insert(mods::RecordId("Character.rcr_sixthstreet_hooligan_melee1_knife_wa"));
  CHECK(
      mods::Resolve(Kind::Vehicle, 77, {.type = mods::NameHash("Bike"), .size = 2}, known).record ==
      mods::RecordId("Vehicle.v_sportbike2_arch_player"));
  CHECK(
      mods::Resolve(Kind::Vehicle, 77, {.type = mods::NameHash("AV"), .size = 4}, known) ==
      mods::Resolved{mods::RecordId("Vehicle.v_standard2_archer_hella_player"), Outcome::Default});
  CHECK(mods::Resolve(Kind::Npc, 77,
                      {.type = mods::NameHash("Human"), .tag = mods::NameHash("Female")}, known)
            .record == mods::RecordId("Character.rcr_sixthstreet_hooligan_melee1_knife_wa"));
  CHECK(mods::NameOf(kHandgun) == "Items.Preset_Lexington_Default");
}

TEST_CASE("every replacement is told: grouped per player and kind, never silent") {
  const std::set<std::uint64_t> here = {kHandgun, kKatana};
  mods::Resolver resolver([&](mods::Kind, std::uint64_t record) { return here.contains(record); });
  std::uint64_t now = 1'000'000;
  // One record of each kind this game lacks, from Judy: each outcome but
  // Same must reach the player.
  std::set<mods::Kind> replaced;
  for (std::size_t kind = 0; kind < mods::kKindCount; ++kind) {
    const auto resolved = resolver.Resolve(static_cast<mods::Kind>(kind), 1000 + kind,
                                           {.type = mods::NameHash("Wea_Katana")}, 2, "Judy", now);
    CHECK(resolved.outcome != mods::Outcome::Same);
    replaced.insert(static_cast<mods::Kind>(kind));
  }
  // Nothing before the gather window: a look's pieces come together.
  CHECK(resolver.TakeNotices(now + 1).empty());
  const auto notices = resolver.TakeNotices(now + mods::Resolver::kGatherUs);
  REQUIRE(notices.size() == mods::kKindCount);
  for (const auto &notice : notices) {
    CHECK(notice.who == "Judy");
    CHECK(notice.count == 1);
    CHECK(replaced.contains(notice.kind));
  }
  // Once per record in the log; told once per kind; listed every time.
  CHECK(resolver.TakeLog().size() == mods::kKindCount);
  resolver.Resolve(mods::Kind::Weapon, 1000, {}, 2, "Judy", now);
  CHECK(resolver.TakeLog().empty());
  // Three pieces of one look at once: one notice, with the count.
  for (std::uint64_t piece = 1; piece <= 3; ++piece)
    resolver.Resolve(mods::Kind::Clothing, 5000 + piece, {}, 3, "Kerry", now);
  const auto look = resolver.TakeNotices(now + mods::Resolver::kGatherUs);
  REQUIRE(look.size() == 1);
  CHECK(look[0].count == 3);
  CHECK(look[0].outcome == mods::Outcome::Skipped);
  // A later one of the same kind from the same player is only listed.
  resolver.Resolve(mods::Kind::Clothing, 5009, {}, 3, "Kerry", now + 10'000'000);
  CHECK(resolver.TakeNotices(now + 20'000'000).empty());
  CHECK(resolver.List().size() == mods::kKindCount + 4);
  // A replacement only the game can check (an NPC's appearance) is told too.
  resolver.Note(mods::Kind::NpcLook, 42, {0, mods::Outcome::Skipped}, 2, "Judy", now);
  CHECK(resolver.List().size() == mods::kKindCount + 5);
  resolver.Note(mods::Kind::NpcLook, 43, {0, mods::Outcome::Skipped}, 4, "River", now);
  // Judy's NPC looks were told already; River's are news.
  const auto later = resolver.TakeNotices(now + mods::Resolver::kGatherUs);
  REQUIRE(later.size() == 1);
  CHECK(later[0].who == "River");
  // What a record this game has gives: nothing to say.
  resolver.Resolve(mods::Kind::Weapon, kKatana, {}, 2, "Judy", now);
  CHECK(resolver.TakeLog().size() == 3 + 1 + 1 + 1); // the look's 3, Kerry's 4th, the two looks
  // Each owner is told which kinds their partner sees replaced, once per new kind.
  const auto told = resolver.TakeTold();
  REQUIRE(told.size() == 3);
  CHECK(told[0].owner == 2);
  CHECK(told[0].kinds == (1u << mods::kKindCount) - 1);
  CHECK(resolver.TakeTold().empty());
}

TEST_CASE(
    "a record checked outside the resolver's lock: the check is the caller's, the memo wins") {
  // The plugin keeps the resolver under a lock the net thread takes every
  // pass: the game's checks (TweakDB, the depot) are made before it, never
  // by the resolver under it.
  int checks = 0;
  mods::Resolver resolver([&](mods::Kind, std::uint64_t) {
    ++checks;
    return false;
  });
  const std::uint64_t now = 1'000'000;
  const std::set<std::uint64_t> here = {kHandgun};
  const auto known = [&](mods::Kind, std::uint64_t record) { return here.contains(record); };
  CHECK(resolver.Memoized(mods::Kind::Weapon, 0, {}));
  CHECK_FALSE(resolver.Memoized(mods::Kind::Weapon, 77, {}));
  const auto decided = mods::Resolve(mods::Kind::Weapon, 77, {}, known);
  REQUIRE(decided.outcome != mods::Outcome::Same);
  CHECK(resolver.Resolve(mods::Kind::Weapon, 77, {}, 2, "Judy", now, decided) == decided);
  CHECK(checks == 0);
  CHECK(resolver.Memoized(mods::Kind::Weapon, 77, {}));
  // Told and listed as any other replacement.
  CHECK(resolver.TakeLog().size() == 1);
  CHECK(resolver.List().size() == 1);
  CHECK(resolver.TakeNotices(now + mods::Resolver::kGatherUs).size() == 1);
  // Another thread's answer came first: it stays (one record, one answer).
  CHECK(resolver.Resolve(mods::Kind::Weapon, 77, {}, 2, "Judy", now, {77, mods::Outcome::Same}) ==
        decided);
  // Memoized: the plain Resolve makes no check either.
  CHECK(resolver.Resolve(mods::Kind::Weapon, 77, {}, 2, "Judy", now) == decided);
  CHECK(checks == 0);
  // A record this game has: nothing to tell.
  CHECK(resolver
            .Resolve(mods::Kind::Weapon, kHandgun, {}, 2, "Judy", now,
                     mods::Resolve(mods::Kind::Weapon, kHandgun, {}, known))
            .outcome == mods::Outcome::Same);
  CHECK(resolver.TakeLog().empty());
  CHECK(checks == 0);
}

TEST_CASE("two games say their mods at a join; names only when asked") {
  const auto port = test::NextPort();
  std::string error;
  Identity hostId{"Vee"};
  hostId.tellsMods = true;
  hostId.mods = {{1, mods::kLocal, "Hair2K"}, {2, mods::kItems, "More Guns"}};
  Identity guestId{"Judy"};
  guestId.tellsMods = true;
  guestId.mods = {{1, mods::kLocal, "Hair2K"},
                  {3, mods::kGameplay, "Quickhack Fixes"},
                  {4, mods::kVehicles, "Bikes"}};
  auto host = Endpoint::Host(hostId, port, error);
  REQUIRE_MESSAGE(host, error);
  auto guest = Endpoint::Join(guestId, "127.0.0.1", port, error);
  REQUIRE_MESSAGE(guest, error);

  std::optional<events::RemoteMods> atHost, atGuest;
  std::optional<events::ModNamesFrom> names;
  bool asked = false, wireNames = false;
  guest->SetReceivedTap([&](const proto::Message &message) {
    if (const auto *list = std::get_if<proto::ModList>(&message); list && list->kind == 0)
      for (const auto &mod : list->mods)
        wireNames |= !mod.name.empty();
  });
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!(atHost && atGuest && names) && std::chrono::steady_clock::now() < deadline) {
    for (const auto &event : host->Update(1ms)) {
      if (const auto *mods = std::get_if<events::RemoteMods>(&event))
        atHost = *mods;
      if (const auto *got = std::get_if<events::ModNamesFrom>(&event))
        names = *got;
    }
    for (const auto &event : guest->Update(1ms))
      if (const auto *mods = std::get_if<events::RemoteMods>(&event))
        atGuest = *mods;
    if (atHost && !asked) {
      host->AskModNames(atHost->player);
      asked = true;
    }
  }
  REQUIRE(atHost);
  REQUIRE(atGuest);
  CHECK(atHost->name == "Judy");
  CHECK(atHost->comparison.theirsOnly.total == 2);
  CHECK(atHost->comparison.oursOnly.total == 1);
  CHECK(atGuest->comparison.theirsOnly.total == 1);
  CHECK(atGuest->comparison.theirsOnly.by[0] == 1); // the host's items
  CHECK_FALSE(wireNames);
  // The names of the two mods the host lacks, none of the others.
  REQUIRE(names);
  REQUIRE(names->mods.size() == 2);
  CHECK(names->mods[0].name == "Quickhack Fixes");
  CHECK(names->mods[1].name == "Bikes");
}

TEST_CASE("a guest's substitutes for another guest's records reach that guest only") {
  const auto port = test::NextPort();
  std::string error;
  auto host = Endpoint::Host({"Vee"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto judy = Endpoint::Join({"Judy"}, "127.0.0.1", port, error);
  auto river = Endpoint::Join({"River"}, "127.0.0.1", port, error);
  REQUIRE((judy && river));
  std::optional<events::SubstitutesFrom> atRiver, atHost;
  bool sent = false;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!(atRiver && atHost) && std::chrono::steady_clock::now() < deadline) {
    for (const auto &event : host->Update(1ms))
      if (const auto *s = std::get_if<events::SubstitutesFrom>(&event))
        atHost = *s;
    for (const auto &event : river->Update(1ms))
      if (const auto *s = std::get_if<events::SubstitutesFrom>(&event))
        atRiver = *s;
    judy->Update(1ms);
    if (!sent && judy->IsJoined() && river->IsJoined() && host->Players().size() == 3) {
      judy->SendSubstitutes({.owner = river->You(), .kinds = 1, .count = 2});
      judy->SendSubstitutes({.owner = host->You(), .kinds = 8, .count = 1});
      sent = true;
    }
  }
  REQUIRE(atRiver);
  CHECK(atRiver->substitutes.seer == judy->You());
  CHECK(atRiver->name == "Judy");
  CHECK(atRiver->substitutes.count == 2);
  REQUIRE(atHost);
  CHECK(atHost->substitutes.kinds == 8);
}
