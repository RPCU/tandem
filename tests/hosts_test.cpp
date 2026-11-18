// What a player types to join, and the recent hosts file.

#include "tandem/hosts.hpp"

#include <doctest/doctest.h>

#include <format>

using namespace tandem;

TEST_CASE("an address alone joins on the default port") {
  const auto target = hosts::Parse("  192.168.1.20 ", 27450);
  REQUIRE(target);
  CHECK(target->address == "192.168.1.20");
  CHECK(target->port == 27450);
}

TEST_CASE("an address with a port, and a host name in any case") {
  const auto target = hosts::Parse("ANYA.LAN:27451", 27450);
  REQUIRE(target);
  CHECK(target->address == "anya.lan");
  CHECK(target->port == 27451);
  CHECK(hosts::Format(*target) == "anya.lan:27451");
}

TEST_CASE("nonsense is refused") {
  CHECK_FALSE(hosts::Parse("", 27450));
  CHECK_FALSE(hosts::Parse("   ", 27450));
  CHECK_FALSE(hosts::Parse("host:", 27450));
  CHECK_FALSE(hosts::Parse("host:0", 27450));
  CHECK_FALSE(hosts::Parse("host:70000", 27450));
  CHECK_FALSE(hosts::Parse("host:12ab", 27450));
  CHECK_FALSE(hosts::Parse("two words", 27450));
  CHECK_FALSE(hosts::Parse("a/b", 27450));
  CHECK_FALSE(hosts::Parse(":27450", 27450));
  // No port to fall back on.
  CHECK_FALSE(hosts::Parse("host", 0));
}

TEST_CASE("recent hosts: newest first, no repeats, capped, names kept") {
  std::vector<hosts::Recent> list;
  hosts::Remember(list, {"10.0.0.1", 27450}, "Judy");
  hosts::Remember(list, {"10.0.0.2", 27450}, "Panam");
  // Back to the first one, its name not known yet this time.
  hosts::Remember(list, {"10.0.0.1", 27450}, "");
  REQUIRE(list.size() == 2);
  CHECK(list[0].target.address == "10.0.0.1");
  CHECK(list[0].name == "Judy");
  CHECK(list[1].name == "Panam");

  for (int i = 0; i < 20; ++i)
    hosts::Remember(list, {std::format("10.0.1.{}", i), 27450}, "");
  CHECK(list.size() == hosts::kMaxRecent);
  CHECK(list[0].target.address == "10.0.1.19");
}

TEST_CASE("recent hosts survive the file") {
  std::vector<hosts::Recent> list;
  hosts::Remember(list, {"anya", 27450}, "V the host");
  hosts::Remember(list, {"10.0.0.2", 27451}, "");
  const auto text = hosts::FormatRecent(list);
  CHECK(text == "10.0.0.2:27451\nanya:27450 V the host\n");
  CHECK(hosts::ParseRecent(text) == list);
  // Lines it did not write are skipped, the rest kept.
  CHECK(hosts::ParseRecent("garbage\n\n10.0.0.3:1 x\r\nnoport name\n10.0.0.3:1 again\n").size() ==
        1);
}
