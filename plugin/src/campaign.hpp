#pragma once

// The player's co-op playthroughs, campaigns.txt next to the settings
// (tandem/campaigns.hpp; docs/architecture/coop-menu.md, "Campaigns"). The
// scripts' side is campaign_natives.cpp (TandemCampaignNative); this is the
// net thread's.

#include <filesystem>
#include <string>

namespace tandem::plugin::campaign {
// Where the book is kept (main.cpp, once at load).
void SetFile(std::filesystem::path file);
// Net thread: the host welcomed our V (its key: campaigns::HostOfAddress or
// HostOfName). The world being played is the V we join it with.
void JoinedWith(const std::string &host);
} // namespace tandem::plugin::campaign
