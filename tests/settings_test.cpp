// tandem.ini changed one key at a time by the settings page.

#include "tandem/settings.hpp"

#include <doctest/doctest.h>

using namespace tandem;

TEST_CASE("a key is rewritten in place, the rest of the file kept") {
  const auto out =
      settings::SetKey("; tandem\nrole = off\nname = V\nport = 27450\n", "name", "Ada");
  REQUIRE(out);
  CHECK(*out == "; tandem\nrole = off\nname = Ada\nport = 27450\n");
}

TEST_CASE("a comment after the value and Windows line endings stay") {
  const auto out = settings::SetKey("recorder = off ; record\r\nname = V\r\n", "recorder", "on");
  REQUIRE(out);
  CHECK(*out == "recorder = on ; record\r\nname = V\r\n");
}

TEST_CASE("a key only in a comment is appended, not uncommented") {
  const auto out = settings::SetKey("; relay = relay.example.org:27460\nrole = off", "relay",
                                    "relay.tandem.lan");
  REQUIRE(out);
  CHECK(*out == "; relay = relay.example.org:27460\nrole = off\nrelay = relay.tandem.lan\n");
}

TEST_CASE("only the first line of a key is rewritten; an empty file gets the line") {
  CHECK(*settings::SetKey("name = A\nname = B\n", "name", "C") == "name = C\nname = B\n");
  CHECK(*settings::SetKey("", "notices", "important") == "notices = important\n");
  CHECK(*settings::SetKey("names = A\n", "name", "B") == "names = A\nname = B\n");
}

TEST_CASE("a value the file cannot hold is refused") {
  CHECK_FALSE(settings::SetKey("", "name", "a\nrole = host"));
  CHECK_FALSE(settings::SetKey("", "name", "V ; comment"));
  CHECK_FALSE(settings::SetKey("", "name", "#1"));
  CHECK_FALSE(settings::SetKey("", "na me", "V"));
}

TEST_CASE("the page changes its own keys, never the session's") {
  CHECK(settings::IsEditable("name"));
  CHECK(settings::IsEditable("friendly_fire"));
  CHECK(settings::IsEditable("relay_token"));
  CHECK_FALSE(settings::IsEditable("role"));
  CHECK_FALSE(settings::IsEditable("address"));
  CHECK_FALSE(settings::IsEditable("port"));
  CHECK_FALSE(settings::IsEditable("join"));
}
