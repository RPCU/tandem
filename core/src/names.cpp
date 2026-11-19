#include "tandem/names.hpp"

#include <algorithm>
#include <format>

namespace tandem::names {
namespace {
// One code point of UTF-8 `text` at `i`, which moves past it; nothing for
// an invalid sequence (an overlong form, a surrogate, a cut one), which
// moves `i` by one byte.
std::optional<char32_t> Next(std::string_view text, std::size_t &i) {
  const auto lead = static_cast<unsigned char>(text[i]);
  if (lead < 0x80) {
    ++i;
    return lead;
  }
  std::size_t extra{};
  char32_t point{};
  if ((lead & 0xE0) == 0xC0) {
    extra = 1;
    point = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    extra = 2;
    point = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    extra = 3;
    point = lead & 0x07;
  } else {
    ++i;
    return std::nullopt;
  }
  if (i + extra >= text.size()) { // cut short
    ++i;
    return std::nullopt;
  }
  for (std::size_t k = 1; k <= extra; ++k) {
    const auto next = static_cast<unsigned char>(text[i + k]);
    if ((next & 0xC0) != 0x80) {
      ++i;
      return std::nullopt;
    }
    point = (point << 6) | (next & 0x3F);
  }
  constexpr char32_t kLeast[] = {0, 0x80, 0x800, 0x10000};
  if (point < kLeast[extra] || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) {
    ++i;
    return std::nullopt;
  }
  i += extra + 1;
  return point;
}

// Not shown as a character: C0 and C1 controls, DEL, and the invisible
// format characters that reorder or hide text (zero widths, direction
// marks and overrides, line separators, the byte order mark).
bool IsControl(char32_t point) {
  return point < 0x20 || point == 0x7F || (point >= 0x80 && point < 0xA0) ||
         (point >= 0x200B && point <= 0x200F) || (point >= 0x2028 && point <= 0x202E) ||
         (point >= 0x2060 && point <= 0x206F) || point == 0xFEFF;
}

// tandem.ini's comment signs: a name holding one could not be saved.
bool IsCommentSign(char32_t point) { return point == ';' || point == '#'; }

std::string_view Trim(std::string_view text) {
  const auto first = text.find_first_not_of(' ');
  if (first == std::string_view::npos)
    return {};
  return text.substr(first, text.find_last_not_of(' ') - first + 1);
}
} // namespace

bool IsDefault(std::string_view name) {
  const auto trimmed = Trim(name);
  return trimmed.empty() || trimmed == kDefault;
}

std::size_t Length(std::string_view text) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < text.size();) {
    Next(text, i);
    ++count;
  }
  return count;
}

std::string_view Problem(std::string_view typed) {
  const auto name = Trim(typed);
  if (name.empty())
    return "empty";
  std::size_t count = 0;
  for (std::size_t i = 0; i < name.size();) {
    const auto point = Next(name, i);
    if (!point)
      return "not text";
    if (IsControl(*point))
      return "a control character";
    if (IsCommentSign(*point))
      return "; or #";
    ++count;
  }
  return count > kMaxLength ? "too long" : "";
}

std::optional<std::string> Clean(std::string_view typed) {
  if (!Problem(typed).empty())
    return std::nullopt;
  return std::string(Trim(typed));
}

std::string Sanitize(std::string_view sent) {
  std::string kept;
  for (std::size_t i = 0; i < sent.size();) {
    const auto start = i;
    const auto point = Next(sent, i);
    if (point && !IsControl(*point) && !IsCommentSign(*point))
      kept.append(sent.substr(start, i - start));
  }
  // At most kMaxLength characters, then no space left at either end.
  auto name = std::string(Trim(kept));
  std::size_t count = 0, end = 0;
  while (end < name.size() && count < kMaxLength) {
    Next(name, end);
    ++count;
  }
  name = std::string(Trim(std::string_view(name).substr(0, end)));
  return name.empty() ? std::string(kDefault) : name;
}

bool Same(std::string_view a, std::string_view b) {
  const auto lower = [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  };
  return a.size() == b.size() &&
         std::ranges::equal(a, b, [&](char x, char y) { return lower(x) == lower(y); });
}

std::string Unique(std::string_view wanted, const std::vector<std::string> &taken) {
  const auto free = [&](std::string_view name) {
    return std::ranges::none_of(taken, [&](const std::string &other) { return Same(name, other); });
  };
  if (free(wanted))
    return std::string(wanted);
  // At most one more number than there are names taken.
  for (std::size_t number = 2;; ++number)
    if (auto name = std::format("{} ({})", wanted, number); free(name))
      return name;
}
} // namespace tandem::names
