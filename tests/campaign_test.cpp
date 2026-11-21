// A shared campaign (docs/architecture/campaign.md): the lineage rules
// (same, older, a fast-forward, a fork), the group's verdict at a join, the
// copy's record and its archive, the import into a new save slot, and the
// copies said through a session over loopback.

#include "ports.hpp"
#include "tandem/campaign.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/report.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>

#include <unistd.h>

using namespace std::chrono_literals;
using namespace tandem;
using campaign::Copy;
using campaign::Relation;
using campaign::Verdict;

namespace {
namespace fs = std::filesystem;

struct Scratch {
  fs::path root;
  explicit Scratch(const char *name) {
    root = fs::temp_directory_path() / std::format("tandem-campaign-{}-{}", name, getpid());
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~Scratch() { fs::remove_all(root); }
  void Put(const fs::path &file, std::string_view text) const {
    const auto path = root / file;
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
  }
  std::string Get(const fs::path &file) const {
    std::ifstream in(root / file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
  }
};

// The campaign's first copy, by Judy, and the copies that go on from it.
const Copy kStart = campaign::Start(0xCA, 0xA0, "Judy", 1'000);

Copy Saved(Copy copy, std::uint64_t at, std::string who, std::string quest = "") {
  copy.savedUnix = at;
  copy.who = std::move(who);
  copy.quest = std::move(quest);
  return copy;
}

campaign::Member Of(proto::PlayerId player, std::string name, Copy copy) {
  return {.player = player, .name = std::move(name), .copy = std::move(copy)};
}
} // namespace

TEST_CASE("campaign: a copy goes on from another, a generation each time") {
  CHECK(kStart.Is());
  CHECK(kStart.generation == 0);
  CHECK(kStart.ancestors.empty());
  const auto next = campaign::Advance(kStart, 0xA1);
  CHECK(next.campaign == kStart.campaign);
  CHECK(next.generation == 1);
  CHECK(next.ancestors == std::vector<std::uint64_t>{0xA0});
  const auto third = campaign::Advance(next, 0xA2);
  CHECK(third.ancestors == std::vector<std::uint64_t>{0xA1, 0xA0});
  // The oldest go past kMaxAncestors; the generation keeps counting.
  auto copy = kStart;
  for (std::uint64_t i = 1; i <= campaign::kMaxAncestors + 5; ++i)
    copy = campaign::Advance(copy, 0xA0 + i);
  CHECK(copy.ancestors.size() == campaign::kMaxAncestors);
  CHECK(copy.generation == campaign::kMaxAncestors + 5);
  CHECK(copy.ancestors.front() == 0xA0 + campaign::kMaxAncestors + 4);
}

TEST_CASE("campaign: the save's words carry the copy and nothing else") {
  const auto copy = campaign::Advance(campaign::Advance(kStart, 0xA1), 0xA2);
  const auto words = campaign::Pack(copy);
  CHECK(words == std::vector<std::uint64_t>{0xCA, 0xA2, 2, 0xA1, 0xA0});
  const auto back = campaign::Unpack(words);
  REQUIRE(back);
  CHECK(back->copy == 0xA2);
  CHECK(back->ancestors == copy.ancestors);
  CHECK(back->who.empty()); // the record's
  CHECK(campaign::Pack(Copy{}).empty());
  CHECK_FALSE(campaign::Unpack({}));
  const std::uint64_t ownAncestor[] = {0xCA, 0xA2, 1, 0xA2};
  CHECK_FALSE(campaign::Unpack(ownAncestor));
  const std::uint64_t tooManyAncestors[] = {0xCA, 0xA2, 1, 0xA1, 0xA0};
  CHECK_FALSE(campaign::Unpack(tooManyAncestors));
  const std::uint64_t noCampaign[] = {0, 0xA2, 0};
  CHECK_FALSE(campaign::Unpack(noCampaign));
}

TEST_CASE("campaign: same, older, a fast-forward, a fork") {
  const auto ours = Saved(campaign::Advance(kStart, 0xA1), 2'000, "Vee", "The Pickup");
  // The same copy, saved at the same time.
  CHECK(campaign::Relate(ours, ours) == Relation::Same);
  // The same copy saved later on its machine: ahead on one line.
  auto later = ours;
  later.savedUnix = 3'000;
  CHECK(campaign::Relate(ours, later) == Relation::Behind);
  CHECK(campaign::Relate(later, ours) == Relation::Ahead);
  // Someone went on from ours: a fast-forward to theirs.
  const auto theirs = Saved(campaign::Advance(ours, 0xB2), 4'000, "Judy", "The Heist");
  CHECK(campaign::Relate(ours, theirs) == Relation::Behind);
  CHECK(campaign::Relate(theirs, ours) == Relation::Ahead);
  // Older: ours is the start's child, the start is behind us.
  CHECK(campaign::Relate(ours, kStart) == Relation::Ahead);
  // Each went on from ours separately: a fork, ours their common copy.
  const auto other = Saved(campaign::Advance(ours, 0xC2), 5'000, "River", "Playing for Time");
  CHECK(campaign::Relate(theirs, other) == Relation::Fork);
  CHECK(campaign::CommonAncestor(theirs, other) == 0xA1);
  CHECK(campaign::CommonAncestor(theirs, ours) == 0xA1);
  // Two campaigns, or no campaign.
  const auto unrelated = campaign::Start(0xCB, 0xD0, "Kerry", 1);
  CHECK(campaign::Relate(ours, unrelated) == Relation::Unrelated);
  CHECK(campaign::Relate(ours, Copy{}) == Relation::None);
  CHECK(campaign::NameOf(Relation::Fork) == "fork");
}

TEST_CASE("campaign: the group's verdict at a join") {
  const auto base = Saved(campaign::Advance(kStart, 0xA1), 2'000, "Vee");
  const auto ahead = Saved(campaign::Advance(base, 0xB2), 3'000, "Judy", "The Heist");
  const auto apart = Saved(campaign::Advance(base, 0xC2), 4'000, "River", "Ghost Town");

  SUBCASE("everyone has the same copy") {
    const campaign::Member group[] = {Of(1, "Vee", base), Of(2, "Judy", base)};
    const auto verdict = campaign::Judge(group, 2, 1);
    CHECK(verdict.kind == Verdict::Kind::Same);
    CHECK_FALSE(verdict.selfBehind);
    CHECK_FALSE(verdict.hostBehind);
  }
  SUBCASE("a guest went on from the host's copy: the host is behind") {
    const campaign::Member group[] = {Of(1, "Vee", base), Of(2, "Judy", ahead)};
    const auto atHost = campaign::Judge(group, 1, 1);
    CHECK(atHost.kind == Verdict::Kind::FastForward);
    REQUIRE(atHost.heads.size() == 1);
    CHECK(atHost.heads[0].name == "Judy");
    CHECK(atHost.heads[0].copy.quest == "The Heist");
    CHECK(atHost.selfBehind);
    CHECK(atHost.hostBehind);
    const auto atGuest = campaign::Judge(group, 2, 1);
    CHECK_FALSE(atGuest.selfBehind);
    CHECK(atGuest.hostBehind);
    CHECK(campaign::SameNews(atHost, atHost));
    CHECK_FALSE(campaign::SameNews(atHost, atGuest));
  }
  SUBCASE("the host is ahead: the guest is offered the host's") {
    const campaign::Member group[] = {Of(1, "Judy", ahead), Of(2, "Vee", base)};
    const auto atGuest = campaign::Judge(group, 2, 1);
    CHECK(atGuest.kind == Verdict::Kind::FastForward);
    REQUIRE(atGuest.heads.size() == 1);
    CHECK(atGuest.heads[0].player == 1);
    CHECK(atGuest.selfBehind);
    CHECK_FALSE(atGuest.hostBehind);
  }
  SUBCASE("two went on separately: a fork, both shown") {
    const campaign::Member group[] = {Of(1, "Judy", ahead), Of(2, "River", apart),
                                      Of(3, "Vee", base)};
    const auto atBase = campaign::Judge(group, 3, 1);
    CHECK(atBase.kind == Verdict::Kind::Fork);
    REQUIRE(atBase.heads.size() == 2);
    CHECK(atBase.heads[0].name == "Judy");
    CHECK(atBase.heads[1].name == "River");
    CHECK(atBase.selfBehind);       // behind both: not a head
    CHECK_FALSE(atBase.hostBehind); // the host's is a head
    const auto atRiver = campaign::Judge(group, 2, 1);
    CHECK_FALSE(atRiver.selfBehind);
  }
  SUBCASE("the same copy twice is one head") {
    const campaign::Member group[] = {Of(1, "Vee", base), Of(2, "Judy", ahead),
                                      Of(3, "River", ahead)};
    const auto verdict = campaign::Judge(group, 3, 1);
    CHECK(verdict.kind == Verdict::Kind::FastForward);
    CHECK(verdict.heads.size() == 1);
  }
  SUBCASE("copies of other campaigns, or no campaign hosted, say nothing") {
    const campaign::Member other[] = {Of(1, "Vee", base),
                                      Of(2, "Judy", campaign::Start(0xCB, 0xD0, "Judy", 1))};
    CHECK(campaign::Judge(other, 2, 1).kind == Verdict::Kind::None);
    const campaign::Member none[] = {Of(1, "Vee", Copy{}), Of(2, "Judy", base)};
    CHECK(campaign::Judge(none, 2, 1).kind == Verdict::Kind::None);
  }
}

TEST_CASE("campaign: a copy's record, written and read back") {
  campaign::Record record{
      .copy = Saved(campaign::Advance(kStart, 0xA1), 2'000, "Judy\n", "The Heist"),
      .slot = "ManualSave-12",
      .foreign = true,
      .setAside = false,
      .chosen = 0xA0,
      .mods = {{.name = "Hair Pack 2", .hash = 0x1234, .categories = mods::kLooks}}};
  const auto text = campaign::Write(record);
  CHECK(text.starts_with("tandem-campaign 1\n"));
  const auto back = campaign::Read(text);
  REQUIRE(back);
  CHECK(back->copy.who == "Judy"); // one line
  record.copy.who = "Judy";
  CHECK(*back == record);
  // Not ours, or not a copy.
  CHECK_FALSE(campaign::Read("hello"));
  CHECK_FALSE(campaign::Read("tandem-campaign 1\ncampaign = 00000000000000ca\n"));
  // A slot that is not a folder name is forgotten, not followed.
  auto odd = record;
  odd.slot = "../ManualSave-1";
  CHECK(campaign::Read(campaign::Write(odd))->slot.empty());
  // Texts are one line of at most kMaxText bytes, never half a character.
  const auto cut = campaign::Clean(std::string(campaign::kMaxText - 1, 'a') + "\xC3\xA9");
  CHECK(cut == std::string(campaign::kMaxText - 1, 'a'));
}

TEST_CASE("campaign: the folder of records, our newest copy, a newer one elsewhere") {
  Scratch scratch("records");
  const auto folder = scratch.root / "lineage";
  const auto old = Saved(campaign::Advance(kStart, 0xA1), 2'000, "Vee");
  const auto mine = Saved(campaign::Advance(old, 0xA2), 3'000, "Vee");
  const auto theirs = Saved(campaign::Advance(mine, 0xB3), 5'000, "Judy");
  const auto aside = Saved(campaign::Advance(old, 0xC2), 9'000, "Vee");
  std::string error;
  REQUIRE(campaign::SaveRecord(folder, {.copy = old, .slot = "AutoSave-1"}, error));
  REQUIRE(campaign::SaveRecord(folder, {.copy = mine, .slot = "ManualSave-3"}, error));
  REQUIRE(campaign::SaveRecord(folder, {.copy = theirs, .foreign = true}, error));
  REQUIRE(campaign::SaveRecord(folder, {.copy = aside, .setAside = true}, error));
  // Written again: replaced, not doubled.
  REQUIRE(campaign::SaveRecord(folder, {.copy = mine, .slot = "ManualSave-4"}, error));
  scratch.Put("lineage/junk.txt", "not a record");
  const auto records = campaign::LoadRecords(folder);
  CHECK(records.size() == 4);
  const auto newest = campaign::NewestOf(records, 0xCA);
  REQUIRE(newest);
  CHECK(newest->copy.copy == 0xA2); // not the foreign one, not the one set aside
  CHECK(newest->slot == "ManualSave-4");
  CHECK_FALSE(campaign::NewestOf(records, 0xCB));
  CHECK(campaign::Find(records, 0xCA, 0xA1)->slot == "AutoSave-1");
  // Judy's copy went on from ours: we are about to host an older one.
  const auto newer = campaign::NewerElsewhere(records, mine);
  REQUIRE(newer);
  CHECK(newer->copy.who == "Judy");
  CHECK_FALSE(campaign::NewerElsewhere(records, theirs));
}

TEST_CASE("campaign: an archive holds the record and the save, and nothing else") {
  const campaign::Record record{.copy = Saved(kStart, 1'500, "Judy", "The Rescue"),
                                .slot = "ManualSave-4"};
  const campaign::File files[] = {{"metadata.9.json", "{\"name\": \"ManualSave-4\"}"},
                                  {"sav.dat", std::string("SAVE\0data", 9)}};
  const auto bytes = campaign::MakeArchive(record, files);
  std::string error;
  const auto archive = campaign::ReadArchive(bytes, error);
  REQUIRE_MESSAGE(archive, error);
  CHECK(archive->record == record);
  REQUIRE(archive->files.size() == 2);
  CHECK(archive->files[1].data == std::string("SAVE\0data", 9));
  // The import list reads only the record, from the start of the file.
  const auto first = 30 + campaign::kRecordEntry.size() + campaign::Write(record).size();
  const auto peeked = campaign::PeekRecord(std::span(bytes).first(first));
  REQUIRE(peeked);
  CHECK(peeked->copy.quest == "The Rescue");

  // A file that is not a save's, a way out of the slot, no save at all.
  const auto refused = [&](std::vector<report::Entry> entries) {
    std::string why;
    CHECK_FALSE(campaign::ReadArchive(report::Zip(entries), why));
    return why;
  };
  const report::Entry lineage{std::string(campaign::kRecordEntry), campaign::Write(record)};
  CHECK(refused({lineage, {"save/../../evil.dll", "x"}, {"save/sav.dat", "x"}}) != "");
  CHECK(refused({lineage, {"r6/scripts/evil.reds", "x"}, {"save/sav.dat", "x"}}) != "");
  CHECK(refused({lineage, {"save/metadata.9.json", "{}"}}) ==
        "the archive holds no save (sav.dat)");
  CHECK(refused({{"save/sav.dat", "x"}}) == "not a campaign archive of tandem's (no lineage)");
  CHECK(refused({lineage, {"save/sav.dat", "x"}, {"save/sav.dat", "y"}}) ==
        "the archive holds sav.dat twice");
  // A damaged one (a byte of the record's text).
  auto damaged = bytes;
  damaged[30 + campaign::kRecordEntry.size() + 10] ^= std::byte{0x40};
  CHECK_FALSE(campaign::ReadArchive(damaged, error));
  CHECK(error == "tandem-campaign.txt is damaged (its checksum differs)");
}

TEST_CASE("campaign: a new manual slot is one past the highest") {
  const std::string slots[] = {"AutoSave-3", "ManualSave-2", "ManualSave-10", "QuickSave-1",
                               "ManualSave-x"};
  CHECK(campaign::NextManualSlot(slots) == "ManualSave-11");
  CHECK(campaign::NextManualSlot({}) == "ManualSave-0");
}

TEST_CASE("campaign: exported to a zip, imported as a new save slot, never over one") {
  Scratch scratch("transfer");
  const auto saves = scratch.root / "saves";
  const auto campaigns = scratch.root / "campaigns";
  scratch.Put("saves/ManualSave-4/sav.dat", "the save");
  scratch.Put("saves/ManualSave-4/metadata.9.json", "{\"name\": \"ManualSave-4\", \"x\": 1}");
  scratch.Put("saves/ManualSave-4/screenshot.png", "png");
  scratch.Put("saves/AutoSave-0/sav.dat", "an autosave");
  const auto since = fs::file_time_type::clock::now() - 1h;
  CHECK(campaign::NewestSlot(saves, since) != "");
  CHECK(campaign::NewestSlot(saves, fs::file_time_type::clock::now() + 1h) == "");

  const campaign::Record record{
      .copy = Saved(campaign::Advance(kStart, 0xA1), 1'795'000'000, "Judy Á.", "The Heist"),
      .mods = {{.name = "Hair Pack", .hash = 1, .categories = mods::kLooks},
               {.name = "Better UI", .hash = 2, .categories = mods::kLocal},
               {.name = "Bikes", .hash = 3, .categories = mods::kVehicles}}};
  const auto exported = campaign::Export(saves / "ManualSave-4", record, campaigns, 1'795'000'000);
  REQUIRE_MESSAGE(!exported.zip.empty(), exported.error);
  CHECK(exported.zip.parent_path() == campaigns);
  CHECK(exported.zip.filename().string().starts_with("campaign-Judy____-"));
  CHECK(exported.zip.filename().string().ends_with("-g1.zip"));
  // Twice: a second file, the first kept.
  const auto again = campaign::Export(saves / "ManualSave-4", record, campaigns, 1'795'000'000);
  CHECK(again.zip != exported.zip);

  const auto listed = campaign::ListArchives(campaigns);
  REQUIRE(listed.size() == 2);
  CHECK(listed[0].record.copy.who == "Judy Á.");
  CHECK(listed[0].record.slot == "ManualSave-4");
  // What the importer lacks: the mods that change more than one game's look.
  const mods::Mod ours[] = {{.name = "Hair Pack", .hash = 1, .categories = mods::kLooks}};
  CHECK(campaign::MissingMods(listed[0].record.mods, ours) == std::vector<std::string>{"Bikes"});

  const auto installed = campaign::Import(exported.zip, saves);
  REQUIRE_MESSAGE(!installed.slot.empty(), installed.error);
  CHECK(installed.slot == "ManualSave-5");
  CHECK(installed.record.slot == "ManualSave-5");
  CHECK(installed.record.copy.copy == 0xA1);
  CHECK(scratch.Get("saves/ManualSave-5/sav.dat") == "the save");
  CHECK(scratch.Get("saves/ManualSave-5/screenshot.png") == "png");
  // Its metadata names its own slot now; the original is untouched.
  CHECK(scratch.Get("saves/ManualSave-5/metadata.9.json") ==
        "{\"name\": \"ManualSave-5\", \"x\": 1}");
  CHECK(scratch.Get("saves/ManualSave-4/metadata.9.json") ==
        "{\"name\": \"ManualSave-4\", \"x\": 1}");
  // Imported again: another new slot.
  CHECK(campaign::Import(exported.zip, saves).slot == "ManualSave-6");
  CHECK(scratch.Get("saves/ManualSave-4/sav.dat") == "the save");
  // No saves folder: nothing made.
  CHECK(campaign::Import(exported.zip, scratch.root / "nowhere").slot.empty());
  CHECK_FALSE(fs::exists(scratch.root / "nowhere"));
}

TEST_CASE("campaign: every player's copy reaches every other player, stamped by the host") {
  const auto port = test::NextPort();
  std::string error;
  auto host = Endpoint::Host({"Vee"}, port, error);
  REQUIRE_MESSAGE(host, error);
  auto judy = Endpoint::Join({"Judy"}, "127.0.0.1", port, error);
  auto river = Endpoint::Join({"River"}, "127.0.0.1", port, error);
  REQUIRE((judy && river));
  const auto hosted = campaign::ToWire(Saved(campaign::Advance(kStart, 0xA1), 2'000, "Vee"));
  auto judys = campaign::ToWire(
      Saved(campaign::Advance(campaign::Advance(kStart, 0xA1), 0xB2), 3'000, "Judy", "The Heist"));
  judys.player = 3; // in someone else's name: the host stamps the sender
  // The host's before anyone is in; Judy's once she is.
  host->SetCampaign(hosted);
  std::map<std::string, std::vector<proto::Campaign>> heard;
  const auto note = [&](const char *who, const std::vector<SessionEvent> &events) {
    for (const auto &event : events)
      if (const auto *c = std::get_if<events::RemoteCampaign>(&event))
        heard[std::format("{} <- {}", who, c->name)].push_back(c->campaign);
  };
  bool sent = false;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline &&
         !(heard.contains("River <- Judy") && heard.contains("Vee <- Judy") &&
           heard.contains("Judy <- Vee") && heard.contains("River <- Vee"))) {
    note("Vee", host->Update(1ms));
    note("Judy", judy->Update(1ms));
    note("River", river->Update(1ms));
    if (!sent && judy->IsJoined() && river->IsJoined() && host->Players().size() == 3) {
      judy->SetCampaign(judys);
      judy->SetCampaign(judys); // a repeat goes nowhere
      sent = true;
    }
  }
  REQUIRE(heard.contains("Vee <- Judy"));
  CHECK(heard["Vee <- Judy"].size() == 1);
  CHECK(heard["Vee <- Judy"][0].player == judy->You());
  CHECK(heard["Vee <- Judy"][0].quest == "The Heist");
  REQUIRE(heard.contains("River <- Judy"));
  CHECK(heard["River <- Judy"][0].player == judy->You());
  REQUIRE(heard.contains("Judy <- Vee"));
  CHECK(heard["Judy <- Vee"][0].copy == 0xA1);
  CHECK(heard["Judy <- Vee"][0].player == host->You());
  CHECK(heard.contains("River <- Vee"));
  CHECK_FALSE(heard.contains("Judy <- Judy")); // never ours back
  // And the verdict every game draws from them: Judy is ahead of the host.
  const campaign::Member group[] = {
      {.player = host->You(), .name = "Vee", .copy = campaign::FromWire(hosted)},
      {.player = judy->You(), .name = "Judy", .copy = campaign::FromWire(heard["Vee <- Judy"][0])}};
  const auto verdict = campaign::Judge(group, host->You(), host->You());
  CHECK(verdict.kind == Verdict::Kind::FastForward);
  CHECK(verdict.hostBehind);
}
