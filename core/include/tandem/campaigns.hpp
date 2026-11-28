#pragma once

// The player's co-op playthroughs, as this game knows them (campaigns.txt,
// next to the settings; docs/architecture/coop-menu.md, "Campaigns"):
//
// - the host's co-op campaigns: playthroughs the co-op menu hosted (Start a
//   new co-op game, Host a saved game, Host this game). The main menu's
//   Continue co-op campaign loads the newest save of one;
// - the guest's V for each host: the playthrough it last joined that host
//   with. The next join of that host offers its newest save first.
//
// The game names a playthrough only in its list of saves (the main menu's
// SaveMetadataInfo.playthroughID). A world played is known by the save the
// menu loaded it from; else by its playthrough time at a load or a save,
// kept as pending and matched against that list the next time the main
// menu reads it (a save's playthrough time is the world's at that moment).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::campaigns {
// One save of the game's list, as the main menu reads it.
struct Save {
  std::int32_t index{-1}; // the game's (0: the newest)
  std::string playthrough;
  double seconds{}; // its playthrough time
  std::int32_t level{};
  std::string location;  // where it was made, in the game's words
  std::uint64_t stamp{}; // when it was made (the game's timestamp, only compared)
  bool modded{};

  bool operator==(const Save &) const = default;
};

// A world whose playthrough is not known yet: its playthrough time at a load
// or a save, and what it is.
struct Pending {
  std::string host; // "": one of our co-op campaigns; else the host its V joined
  double seconds{};

  bool operator==(const Pending &) const = default;
};

// The V a guest last joined a host with.
struct Guest {
  std::string host; // HostKey
  std::string playthrough;

  bool operator==(const Guest &) const = default;
};

struct Book {
  std::vector<std::string> campaigns; // newest first
  std::vector<Guest> guests;          // newest first, one per host
  std::vector<Pending> pending;       // newest first

  bool operator==(const Book &) const = default;
};

// The world being played: its playthrough when the menu loaded it from a
// save it read, and its playthrough time at its load and at each save.
struct World {
  std::string playthrough; // "": not known
  std::vector<double> seconds;
};

// A save's playthrough time and a world's agree within this.
inline constexpr double kMatchSeconds = 5.0;
// Earlier than this a world has no save of its own to match (a new game).
inline constexpr double kMinSeconds = 60.0;
inline constexpr std::size_t kMaxCampaigns = 16;
inline constexpr std::size_t kMaxGuests = 16;
inline constexpr std::size_t kMaxPending = 32;

// The file: "campaign ID", "guest ID HOST", "pending SECONDS [HOST]" lines;
// others are skipped.
Book Parse(std::string_view text);
std::string Format(const Book &book);

// The key a host goes by: "address:port" joined by address; "name:NAME" (in
// lower case) joined by a join code, which changes from session to session.
std::string HostOfAddress(std::string_view address, std::uint16_t port);
std::string HostOfName(std::string_view name);

// This world is a co-op campaign; its V joined `host`. True: the book changed.
bool MarkCampaign(Book &book, const World &world);
bool JoinedWith(Book &book, const World &world, std::string_view host);
// It is no co-op campaign (any more): forgotten. True: the book changed.
bool Unmark(Book &book, const World &world);
bool IsCampaign(const Book &book, std::string_view playthrough);

// The menu's list read: each pending world whose time matches saves of one
// playthrough alone becomes that playthrough. True: the book changed.
bool Learn(Book &book, const std::vector<Save> &saves);

// The newest save of a co-op campaign; of the V `host` was last joined with.
std::optional<Save> LatestCampaign(const Book &book, const std::vector<Save> &saves);
std::optional<Save> LatestOf(const Book &book, std::string_view host,
                             const std::vector<Save> &saves);
} // namespace tandem::campaigns
