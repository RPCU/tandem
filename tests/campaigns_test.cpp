// The player's co-op playthroughs: campaigns, the guest's V per host, and
// worlds learnt from the main menu's list of saves.

#include "tandem/campaigns.hpp"

#include <doctest/doctest.h>

#include <format>

using namespace tandem;
using campaigns::Save;

namespace {
Save SaveOf(std::int32_t index, std::string playthrough, double seconds, std::uint64_t stamp) {
  return {.index = index,
          .playthrough = std::move(playthrough),
          .seconds = seconds,
          .level = 12,
          .location = "Watson",
          .stamp = stamp};
}
} // namespace

TEST_CASE("campaigns: a world the menu loaded is known at once") {
  campaigns::Book book;
  const campaigns::World world{.playthrough = "pt-host", .seconds = {3600.0}};
  CHECK(campaigns::MarkCampaign(book, world));
  // Marked again: nothing new.
  CHECK_FALSE(campaigns::MarkCampaign(book, world));
  CHECK(campaigns::IsCampaign(book, "pt-host"));
  const std::vector<Save> saves{SaveOf(0, "pt-solo", 100.0, 30), SaveOf(1, "pt-host", 3600.0, 20),
                                SaveOf(2, "pt-host", 1800.0, 10)};
  const auto latest = campaigns::LatestCampaign(book, saves);
  REQUIRE(latest);
  CHECK(latest->index == 1);
  CHECK_FALSE(campaigns::LatestOf(book, "name:judy", saves));
}

TEST_CASE("campaigns: a new game is learnt from a save's playthrough time") {
  campaigns::Book book;
  // Its first minute is no time to match; its first autosave is.
  campaigns::World world{.seconds = {2.0}};
  CHECK_FALSE(campaigns::MarkCampaign(book, world));
  world.seconds.push_back(905.3);
  CHECK(campaigns::MarkCampaign(book, world));
  REQUIRE(book.pending.size() == 1);
  CHECK(book.pending[0].host.empty());
  // A list without it: still pending.
  CHECK_FALSE(campaigns::Learn(book, {SaveOf(0, "pt-solo", 400.0, 5)}));
  // Its autosave, 2 s off: learnt, and no longer pending.
  const std::vector<Save> saves{SaveOf(0, "pt-new", 903.4, 9), SaveOf(1, "pt-solo", 400.0, 5)};
  CHECK(campaigns::Learn(book, saves));
  CHECK(book.pending.empty());
  CHECK(campaigns::IsCampaign(book, "pt-new"));
  CHECK(campaigns::LatestCampaign(book, saves)->index == 0);
}

TEST_CASE("campaigns: a time two playthroughs share is not learnt") {
  campaigns::Book book;
  CHECK(campaigns::MarkCampaign(book, {.seconds = {1200.0}}));
  CHECK_FALSE(campaigns::Learn(book, {SaveOf(0, "a", 1201.0, 2), SaveOf(1, "b", 1199.0, 1)}));
  CHECK(book.pending.size() == 1);
  CHECK(book.campaigns.empty());
}

TEST_CASE("campaigns: the V a guest joined each host with, newest wins") {
  campaigns::Book book;
  const auto judy = campaigns::HostOfName("  Judy ");
  CHECK(judy == "name:judy");
  CHECK(campaigns::HostOfAddress("192.0.2.10", 27450) == "192.0.2.10:27450");
  CHECK(campaigns::JoinedWith(book, {.playthrough = "pt-a", .seconds = {700.0}}, judy));
  // Joined later with another V (a new one, learnt from its time).
  CHECK(campaigns::JoinedWith(book, {.seconds = {2400.0}}, judy));
  std::vector<Save> saves{SaveOf(0, "pt-b", 2400.0, 50), SaveOf(1, "pt-a", 700.0, 40),
                          SaveOf(2, "pt-b", 3000.0, 60)};
  CHECK(campaigns::LatestOf(book, judy, saves)->index == 1);
  CHECK(campaigns::Learn(book, saves));
  // Its newest save, by the game's timestamp.
  CHECK(campaigns::LatestOf(book, judy, saves)->index == 2);
  CHECK(book.guests.size() == 1);
  // An unknown host has none; a host key of two lines is no key.
  CHECK_FALSE(campaigns::LatestOf(book, "name:panam", saves));
  CHECK_FALSE(campaigns::JoinedWith(book, {.playthrough = "pt-a"}, "a\nb"));
}

TEST_CASE("campaigns: unmarked, and capped") {
  campaigns::Book book;
  for (int i = 0; i < 40; ++i)
    campaigns::MarkCampaign(book, {.playthrough = std::format("pt-{}", i)});
  CHECK(book.campaigns.size() == campaigns::kMaxCampaigns);
  CHECK(book.campaigns.front() == "pt-39");
  CHECK(campaigns::Unmark(book, {.playthrough = "pt-39"}));
  CHECK_FALSE(campaigns::IsCampaign(book, "pt-39"));
  for (int i = 0; i < 40; ++i)
    campaigns::MarkCampaign(book, {.seconds = {100.0 + 20.0 * i}});
  CHECK(book.pending.size() == campaigns::kMaxPending);
  CHECK(campaigns::Unmark(book, {.seconds = {880.0}}));
}

TEST_CASE("campaigns: the book survives its file") {
  campaigns::Book book;
  campaigns::MarkCampaign(book, {.playthrough = "pt-host"});
  campaigns::JoinedWith(book, {.playthrough = "pt-guest"}, campaigns::HostOfName("V the host"));
  campaigns::JoinedWith(book, {.seconds = {4000.0}}, "192.0.2.10:27450");
  campaigns::MarkCampaign(book, {.seconds = {65.0}});
  const auto text = campaigns::Format(book);
  CHECK(text == "campaign pt-host\nguest pt-guest name:v the host\npending 65.0\n"
                "pending 4000.0 192.0.2.10:27450\n");
  CHECK(campaigns::Parse(text) == book);
  // Lines it did not write are skipped, the rest kept.
  const auto mixed = campaigns::Parse("junk\ncampaign\ncampaign two words\nguest only\n"
                                      "pending x\npending -3\ncampaign pt-x\r\n");
  CHECK(mixed.campaigns == std::vector<std::string>{"pt-x"});
  CHECK(mixed.guests.empty());
  CHECK(mixed.pending.empty());
}
