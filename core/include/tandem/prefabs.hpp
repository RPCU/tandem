#pragma once

// The host's quest prefabs in a guest's world, and every player's story
// gates (docs/architecture/story.md, "Quest prefabs" and "Story gates";
// engine facts: engine-knowledge §13.111, §13.112). Rules only: the plugin
// reads and changes the engine's prefab modes, the scripts read the facts.

#include "tandem/protocol.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace tandem::prefabs {
// What our own game says of one prefab (the quests system's prefab handler).
struct Local {
  bool on{};   // its mode is ForceLoad now
  bool held{}; // one of our own quest phases asked for it (its count)
};
// One change to make in our world: `prefab` on (ForceLoad) or off (Disable).
struct Step {
  std::uint64_t prefab{};
  bool on{};
  friend bool operator==(const Step &, const Step &) = default;
};

// A guest's rule. Grouped with the host, every prefab of the host's set is
// on in our world too. The ones we turned on are ours, and the only ones we
// ever turn off: when the host's set drops one, when we go apart, when we
// leave. One on before we came to it (our own quest's) is never touched;
// one of ours that our own quest asked for meanwhile is left on and no
// longer ours (that quest releases it the engine's way). One of ours our
// own quest turned off (its phase ended) while the host still has it is put
// on again.
class Mirror {
public:
  void Host(const proto::WorldPrefabs &message) { m_host = message.prefabs; }
  const std::vector<std::uint64_t> &HostSet() const { return m_host; }
  using Query = std::function<Local(std::uint64_t)>;
  // Each check: what to change now (the plugin makes the steps, in order).
  std::vector<Step> Update(bool grouped, const Query &local);
  // The session is over or the host vanished: ours off (unless held), the
  // host's set forgotten.
  std::vector<Step> Lift(const Query &local);
  const std::set<std::uint64_t> &Ours() const { return m_ours; }
  // Ours put on again after our own quest turned them off.
  std::uint32_t Reapplied() const { return m_reapplied; }

private:
  std::vector<Step> Release(bool keepWanted, const Query &local);

  std::vector<std::uint64_t> m_host;
  std::set<std::uint64_t> m_ours;
  std::uint32_t m_reapplied{};
};
} // namespace tandem::prefabs

namespace tandem::gates {
// The gates a story keeps closed early (StoryGates' bits).
enum class Gate : std::uint8_t { Watson = 0, Dogtown = 1 };
inline constexpr std::array kGates{Gate::Watson, Gate::Dogtown};
bool Open(std::uint8_t flags, Gate gate);
// The V stands on the gate's inner side now: in Watson (the lockdown's) or in
// Dogtown (its gate's).
bool Inside(std::uint8_t flags, Gate gate);

// At the join, and whenever a story opens one: the gates the host's story
// has open and the guest's keeps closed (told to both players in words).
std::vector<Gate> Behind(std::uint8_t host, std::uint8_t guest);

// In play, a guest meets the barrier the host passed: its story keeps the
// gate closed, the host's has it open, the host's V is on the other side
// and near (it just crossed where the guest stands): the Watson lockdown,
// the guest in Watson and the host out of it; Dogtown, the guest outside
// and the host in.
inline constexpr float kBarrierReach = 250.f;
// The same, wherever the two stand (the gate's sides only).
bool Past(std::uint8_t host, std::uint8_t guest, Gate gate);
std::optional<Gate> Met(std::uint8_t host, std::uint8_t guest, bool hostKnown, float distance);

// Once per crossing: told when the guest meets it, again only once the host
// has been back on the guest's side (or the guest's story opened it).
class Watcher {
public:
  std::optional<Gate> Update(std::uint8_t host, std::uint8_t guest, bool hostKnown, float distance);
  void Clear() { m_told = 0; }

private:
  std::uint8_t m_told{}; // a bit per gate
};
} // namespace tandem::gates
