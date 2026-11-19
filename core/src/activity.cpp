#include "tandem/activity.hpp"

#include <cmath>

namespace tandem::activity {
proto::Activity Sanitized(proto::Activity state) {
  if (state.kind > proto::ActivityKind::Contest)
    state.kind = proto::ActivityKind::None;
  if (state.kind == proto::ActivityKind::None)
    return {.player = state.player};
  state.flags &= proto::kActivityFlags;
  const bool finite = std::isfinite(state.anchor.x) && std::isfinite(state.anchor.y) &&
                      std::isfinite(state.anchor.z) && std::isfinite(state.yaw);
  if (!finite || !(state.flags & proto::kActivityAnchored)) {
    state.flags &= static_cast<std::uint8_t>(~proto::kActivityAnchored);
    state.anchor = {};
    state.yaw = 0.f;
  }
  // Lying is a bed's or a workspot's; a body that lies is held where it lies
  // (unanchored, the ground snap would stand it up).
  const bool spot = state.kind == proto::ActivityKind::Workspot ||
                    state.kind == proto::ActivityKind::Sleep ||
                    state.kind == proto::ActivityKind::Shop;
  if (!spot || !(state.flags & proto::kActivityAnchored))
    state.flags &= static_cast<std::uint8_t>(~proto::kActivityLying);
  // A workspot is named only by what plays one.
  if (!spot && state.kind != proto::ActivityKind::Screen &&
      state.kind != proto::ActivityKind::Minigame) {
    state.workspot = 0;
    state.entry = 0;
    state.anim = 0;
  }
  if (state.workspot == 0) {
    state.entry = 0;
    state.anim = 0;
  }
  // A game in a workspot animation runs: frozen is for screens that stop it.
  if (state.kind == proto::ActivityKind::Workspot || state.kind == proto::ActivityKind::Sleep ||
      state.kind == proto::ActivityKind::Contest)
    state.flags &= static_cast<std::uint8_t>(~proto::kActivityFrozen);
  return state;
}

proto::TimeSkipResult SkipArbiter::Conclude(proto::SkipOutcome outcome, proto::PlayerId by,
                                            std::uint8_t why) {
  const proto::TimeSkipResult result{.asker = m_open->from,
                                     .request = m_open->request,
                                     .reason = m_open->reason,
                                     .outcome = outcome,
                                     .by = by,
                                     .why = why,
                                     .fromSeconds = m_open->fromSeconds,
                                     .toSeconds = m_open->toSeconds};
  Reset();
  return result;
}

std::optional<proto::TimeSkipResult> SkipArbiter::Ask(const proto::TimeSkip &skip, bool fromHost,
                                                      std::set<proto::PlayerId> voters,
                                                      Blocker blocker, std::uint64_t nowUs) {
  const auto refused = [&](std::uint8_t why, proto::PlayerId by) {
    return proto::TimeSkipResult{.asker = skip.from,
                                 .request = skip.request,
                                 .reason = skip.reason,
                                 .outcome = proto::SkipOutcome::Refused,
                                 .by = by,
                                 .why = why,
                                 .fromSeconds = skip.fromSeconds,
                                 .toSeconds = skip.toSeconds};
  };
  voters.erase(skip.from);
  if (skip.reason == proto::SkipReason::Quest) {
    // The host's story moved its clock already: nothing to ask, only said.
    // An open request stays open (the host checks its target against the
    // clock when it is applied).
    if (!fromHost)
      return refused(proto::kSkipNotHost, proto::kNoPlayer);
    auto said = refused(proto::kSkipNoReason, proto::kNoPlayer);
    said.outcome = proto::SkipOutcome::Applied;
    return said;
  }
  if (m_open)
    return refused(proto::kSkipBusy, m_open->from);
  // Forward, by a day at most, on the asker's clock (a guest's follows the
  // host's within a minute).
  const auto ahead = static_cast<std::int64_t>(skip.toSeconds) - skip.fromSeconds;
  if (ahead <= 0 || ahead > kMaxSkipSeconds)
    return refused(proto::kSkipNoReason, proto::kNoPlayer);
  if (blocker.why != proto::kSkipNoReason)
    return refused(blocker.why, blocker.by);
  m_open = skip;
  if (voters.empty())
    return Conclude(proto::SkipOutcome::Applied, proto::kNoPlayer, proto::kSkipNoReason);
  m_waiting = std::move(voters);
  m_lapsesAtUs = nowUs + m_answerUs;
  return std::nullopt;
}

bool SkipArbiter::Joins(const proto::TimeSkip &skip) const {
  return m_open && skip.reason == proto::SkipReason::Sleep && skip.from != m_open->from &&
         m_waiting.contains(skip.from);
}

std::optional<proto::TimeSkipResult> SkipArbiter::Vote(const proto::TimeSkipVote &vote) {
  if (!m_open || vote.asker != m_open->from || vote.request != m_open->request)
    return std::nullopt;
  if (vote.from == m_open->from) {
    if (vote.accepted)
      return std::nullopt; // the asker has no vote, only a way out
    return Conclude(proto::SkipOutcome::Withdrawn, vote.from, proto::kSkipNoReason);
  }
  if (!m_waiting.contains(vote.from))
    return std::nullopt; // not asked (a late joiner), or answered already
  if (!vote.accepted)
    return Conclude(proto::SkipOutcome::Declined, vote.from, vote.why);
  m_waiting.erase(vote.from);
  if (m_waiting.empty())
    return Conclude(proto::SkipOutcome::Applied, proto::kNoPlayer, proto::kSkipNoReason);
  return std::nullopt;
}

std::optional<proto::TimeSkipResult> SkipArbiter::Left(proto::PlayerId player) {
  if (!m_open)
    return std::nullopt;
  if (player == m_open->from)
    return Conclude(proto::SkipOutcome::Withdrawn, player, proto::kSkipNoReason);
  // A player that is gone has no say: the ones left decide. The last one
  // owed going is not a yes from anyone who was asked, but everyone still
  // here has accepted (or there is nobody left to disturb).
  if (m_waiting.erase(player) && m_waiting.empty())
    return Conclude(proto::SkipOutcome::Applied, proto::kNoPlayer, proto::kSkipNoReason);
  return std::nullopt;
}

std::optional<proto::TimeSkipResult> SkipArbiter::Block(Blocker blocker) {
  if (!m_open || blocker.why == proto::kSkipNoReason)
    return std::nullopt;
  return Conclude(proto::SkipOutcome::Refused, blocker.by, blocker.why);
}

std::optional<proto::TimeSkipResult> SkipArbiter::Tick(std::uint64_t nowUs) {
  if (!m_open || nowUs < m_lapsesAtUs)
    return std::nullopt;
  return Conclude(proto::SkipOutcome::Lapsed, *m_waiting.begin(), proto::kSkipNoReason);
}
} // namespace tandem::activity
