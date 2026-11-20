#include "tandem/twins.hpp"

#include "tandem/regions.hpp"

#include <algorithm>
#include <tuple>

namespace tandem::twins {
std::vector<Pair> Match(std::span<const Body> theirs, std::span<const Body> ours, float reach) {
  std::vector<Pair> pairs;
  std::vector<bool> taken(ours.size()), matched(theirs.size());
  // 1. The same placed entity.
  for (std::size_t t = 0; t < theirs.size(); ++t) {
    if (theirs[t].entity == 0)
      continue;
    // Named: its twin is the one of that id, or none (never a neighbour).
    matched[t] = true;
    for (std::size_t o = 0; o < ours.size(); ++o)
      if (!taken[o] && ours[o].entity == theirs[t].entity) {
        if (!ours[o].busy) {
          taken[o] = true;
          pairs.push_back({.theirs = t, .ours = o, .identity = true});
        }
        break;
      }
  }
  // 2. The rest by record and place: every pair in reach, nearest first.
  struct Candidate {
    float distance{};
    std::size_t theirs{}, ours{};
  };
  std::vector<Candidate> candidates;
  for (std::size_t t = 0; t < theirs.size(); ++t) {
    if (matched[t])
      continue;
    for (std::size_t o = 0; o < ours.size(); ++o) {
      if (taken[o] || ours[o].busy || ours[o].entity != 0 || ours[o].record != theirs[t].record)
        continue;
      const float distance = regions::Distance(theirs[t].position, ours[o].position);
      if (distance < reach)
        candidates.push_back({distance, t, o});
    }
  }
  std::ranges::sort(candidates, [](const Candidate &a, const Candidate &b) {
    return std::tie(a.distance, a.theirs, a.ours) < std::tie(b.distance, b.theirs, b.ours);
  });
  for (const auto &candidate : candidates) {
    if (matched[candidate.theirs] || taken[candidate.ours])
      continue;
    matched[candidate.theirs] = true;
    taken[candidate.ours] = true;
    pairs.push_back({.theirs = candidate.theirs, .ours = candidate.ours});
  }
  std::ranges::sort(pairs, {}, &Pair::theirs);
  return pairs;
}
} // namespace tandem::twins
