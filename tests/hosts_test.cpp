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

TEST_CASE("join by address: each refusal says why") {
  using P = hosts::Problem;
  const auto problem = [](std::string_view text) { return hosts::Check(text, 27450).problem; };
  CHECK(problem("  ") == P::Empty);
  CHECK(problem("host:") == P::Port);
  CHECK(problem("host:65536") == P::Port);
  CHECK(problem("host:+1") == P::Port);
  CHECK(problem("10.0.0.300") == P::Name);
  CHECK(problem("10.0.0") == P::Name);
  CHECK(problem("12345") == P::Name);
  CHECK(problem("-host.lan") == P::Name);
  CHECK(problem("host..lan") == P::Name);
  CHECK(problem(".lan") == P::Name);
  CHECK(problem(std::string(64, 'a')) == P::Name);
  // IPv6, bracketed with a port or not: well formed, and refused for it.
  CHECK(problem("[fd7a:115c:a1e0::1]:27450") == P::Ipv6);
  CHECK(problem("[::1]") == P::Ipv6);
  CHECK(problem("fd7a:115c:a1e0::1") == P::Ipv6);
  CHECK(problem("[::1]:0") == P::Port);
  CHECK(problem("[::1]27450") == P::Port);
  CHECK(problem("[host]:27450") == P::Name);
  CHECK(problem("[::1") == P::Name);
  CHECK(hosts::ProblemWord(P::Ipv6) == "ipv6");
  CHECK(hosts::ProblemWord(P::None).empty());
}

TEST_CASE("join by address: LAN and VPN addresses") {
  // Tailscale and NetBird hand out addresses in 100.64.0.0/10, and names.
  const auto vpn = hosts::Check("100.101.102.103", 27450);
  REQUIRE(vpn.target);
  CHECK(vpn.problem == hosts::Problem::None);
  CHECK(hosts::Format(*vpn.target) == "100.101.102.103:27450");
  const auto named = hosts::Check("my-pc.tail1234.ts.net:27451", 27450);
  REQUIRE(named.target);
  CHECK(hosts::Format(*named.target) == "my-pc.tail1234.ts.net:27451");
  CHECK(hosts::Parse("my_pc", 27450));
  CHECK(hosts::Parse("0.0.0.0", 27450));
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

TEST_CASE("recent hosts joined by a code: known by name, the newest code kept") {
  std::vector<hosts::Recent> list;
  hosts::Remember(list, {"192.0.2.10", 27450}, "Panam");
  hosts::RememberCode(list, "K7Q2M9", "Judy");
  // The same host another day, with another code: one entry, first.
  hosts::RememberCode(list, "ABC234", "judy");
  // No name to know it by: nothing.
  hosts::RememberCode(list, "ZZZ222", "");
  REQUIRE(list.size() == 2);
  CHECK(list[0].code == "ABC234");
  CHECK(list[0].name == "judy");
  CHECK(list[0].target == hosts::Target{});
  CHECK(list[1].name == "Panam");
  // An address entry of the same name stays apart.
  hosts::Remember(list, {"192.0.2.11", 27450}, "judy");
  CHECK(list.size() == 3);
  const auto text = hosts::FormatRecent(list);
  CHECK(text == "192.0.2.11:27450 judy\ncode:ABC234 judy\n192.0.2.10:27450 Panam\n");
  CHECK(hosts::ParseRecent(text) == list);
  // A code line without a name, or not a code, is skipped.
  CHECK(hosts::ParseRecent("code:ABC234\ncode:abc234 x\ncode:ABC23 x\ncode:ABC234 x\n").size() ==
        1);
}
