// Where a game among several keeps tandem's files (TANDEM_CONFIG, TANDEM_CMD,
// TANDEM_PORT, TANDEM_LOG).

#include "tandem/instance.hpp"

#include <doctest/doctest.h>

using namespace tandem;

TEST_CASE("without overrides everything is next to the DLL, and there is no own log") {
  const auto files = instance::Resolve("/game/red4ext/plugins/tandem", {});
  CHECK(files.config == "/game/red4ext/plugins/tandem/tandem.ini");
  CHECK(files.command == "/game/red4ext/plugins/tandem/tandem.cmd");
  CHECK(files.recent == "/game/red4ext/plugins/tandem/recent.txt");
  CHECK(files.log.empty());
}

TEST_CASE("an instance's files: the recent hosts follow the settings") {
  const auto files =
      instance::Resolve("/game/red4ext/plugins/tandem", {.config = "/instances/b/tandem.ini",
                                                         .command = "/instances/b/run/tandem.cmd",
                                                         .log = "/instances/b/tandem.log"});
  CHECK(files.config == "/instances/b/tandem.ini");
  CHECK(files.recent == "/instances/b/recent.txt");
  CHECK(files.campaigns == "/instances/b/campaigns.txt");
  CHECK(files.command == "/instances/b/run/tandem.cmd");
  CHECK(files.log == "/instances/b/tandem.log");
}

TEST_CASE("each override stands alone, and a relative path starts at the plugin's directory") {
  const auto files =
      instance::Resolve("/game/red4ext/plugins/tandem", {.command = "instances/b/tandem.cmd"});
  CHECK(files.config == "/game/red4ext/plugins/tandem/tandem.ini");
  CHECK(files.recent == "/game/red4ext/plugins/tandem/recent.txt");
  CHECK(files.command == "/game/red4ext/plugins/tandem/instances/b/tandem.cmd");
  CHECK(instance::Resolve("/plugin", {.log = "../logs/b.log"}).log == "/logs/b.log");
}

TEST_CASE("TANDEM_PORT is a whole port or nothing") {
  CHECK(instance::ParsePort("27451") == 27451);
  CHECK(instance::ParsePort(" 27451\r\n") == 27451);
  CHECK(instance::ParsePort("65535") == 65535);
  CHECK_FALSE(instance::ParsePort(""));
  CHECK_FALSE(instance::ParsePort("  "));
  CHECK_FALSE(instance::ParsePort("0"));
  CHECK_FALSE(instance::ParsePort("65536"));
  CHECK_FALSE(instance::ParsePort("27451x"));
  CHECK_FALSE(instance::ParsePort("-1"));
  CHECK_FALSE(instance::ParsePort("27 451"));
}
