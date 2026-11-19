// Players' names: the pseudo typed, what a host shows, unique in a session.

#include "tandem/names.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace tandem;

TEST_CASE("a typed name is trimmed, 1 to 16 characters of text, no control or comment sign") {
  CHECK(names::Clean("  Alice ") == std::string("Alice"));
  CHECK(names::Clean("Hélène") == std::string("Hélène")); // two bytes for é, one character
  CHECK(names::Clean("Jackie Welles") == std::string("Jackie Welles"));
  CHECK(names::Clean("V") == std::string("V"));
  CHECK(names::Clean("0123456789abcdef") == std::string("0123456789abcdef")); // 16
  CHECK(names::Clean("ééééééééééééééééé") == std::nullopt);                   // 17 characters
  CHECK(names::Clean("éééééééééééééééé").has_value());                        // 16, 32 bytes

  CHECK(names::Problem("") == "empty");
  CHECK(names::Problem("   ") == "empty");
  CHECK(names::Problem("0123456789abcdefg") == "too long");
  CHECK(names::Problem("a\tb") == "a control character");
  CHECK(names::Problem("a\x7f") == "a control character");
  CHECK(names::Problem("a\xe2\x80\xae"
                       "b") == "a control character"); // U+202E, right to left
  CHECK(names::Problem("a;b") == "; or #");
  CHECK(names::Problem("#1") == "; or #");
  CHECK(names::Problem("a\xff") == "not text");
  CHECK(names::Problem("\xc3") == "not text");         // cut short
  CHECK(names::Problem("\xc0\xaf") == "not text");     // an overlong '/'
  CHECK(names::Problem("\xed\xa0\x80") == "not text"); // a surrogate
  CHECK(names::Problem("Panam") == "");
}

TEST_CASE("the default name is V, or none") {
  CHECK(names::IsDefault("V"));
  CHECK(names::IsDefault(" V "));
  CHECK(names::IsDefault(""));
  CHECK_FALSE(names::IsDefault("v")); // typed on purpose
  CHECK_FALSE(names::IsDefault("Vik"));
}

TEST_CASE("a host shows whatever a guest sent as a name it can show") {
  CHECK(names::Sanitize("Panam") == "Panam");
  CHECK(names::Sanitize("  Panam  ") == "Panam");
  CHECK(names::Sanitize("Pa\nnam;#") == "Panam");
  CHECK(names::Sanitize("a\xff"
                        "b") == "ab");
  CHECK(names::Sanitize("") == "V");
  CHECK(names::Sanitize("\x01\x02") == "V");
  CHECK(names::Sanitize("tandem-peer-with-a-long-name") == "tandem-peer-with");
  CHECK(names::Length(names::Sanitize(std::string(40, 'x'))) == names::kMaxLength);
  // Cut at a character, never inside one, and no space left at the end.
  CHECK(names::Sanitize("ééééééééééééééééé") == "éééééééééééééééé");
  CHECK(names::Sanitize("fifteen chars.. x") == "fifteen chars..");
}

TEST_CASE("names in a session are unique: V, V (2), V (3)") {
  CHECK(names::Unique("Alice", {}) == "Alice");
  CHECK(names::Unique("Alice", {"Bob"}) == "Alice");
  CHECK(names::Unique("V", {"V"}) == "V (2)");
  CHECK(names::Unique("V", {"V", "V (2)"}) == "V (3)");
  CHECK(names::Unique("V", {"V (2)", "V"}) == "V (3)");
  CHECK(names::Unique("V", {"V", "V (3)"}) == "V (2)"); // a free number again
  CHECK(names::Unique("v", {"V"}) == "v (2)");          // the same to a reader
  CHECK(names::Same("Alice", "ALICE"));
  CHECK_FALSE(names::Same("Alice", "Alicé"));
  CHECK(names::Length("Hélène") == 6);
}
