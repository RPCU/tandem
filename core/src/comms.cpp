#include "tandem/comms.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::comms {
namespace {
float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

bool SameMark(const proto::Mark &a, const proto::Mark &b) {
  return a.from == b.from && a.id == b.id;
}
} // namespace

bool Limiter::Allow(std::uint64_t nowUs) {
  if (m_lastUs != 0 && nowUs > m_lastUs)
    m_tokens = std::min(m_burst, m_tokens + static_cast<double>(nowUs - m_lastUs) / 1e6 * m_rate);
  m_lastUs = nowUs;
  if (m_tokens < 1.0)
    return false;
  m_tokens -= 1.0;
  return true;
}

std::uint8_t LifeOf(proto::MarkKind kind, std::uint8_t asked) {
  if (asked != 0)
    return std::clamp(asked, kMinMarkSeconds, kMaxMarkSeconds);
  switch (kind) {
  case proto::MarkKind::Enemy:
    return 15; // it moves: it follows the NPC, but an old one misleads
  case proto::MarkKind::Danger:
    return 10;
  case proto::MarkKind::Target:
    return 60; // until it dies, is cleared or replaced; this is the backstop
  case proto::MarkKind::Device:
  case proto::MarkKind::Loot:
    return 30;
  case proto::MarkKind::Spot:
  case proto::MarkKind::Vehicle:
  case proto::MarkKind::None:
    break;
  }
  return 20;
}

bool SameThing(const proto::Mark &a, const proto::Mark &b, float metres) {
  if (a.target.kind != proto::TargetKind::None || b.target.kind != proto::TargetKind::None)
    return a.target == b.target;
  return Distance(a.position, b.position) <= metres;
}

void Pinger::Prune(std::uint64_t nowUs) {
  std::erase_if(m_live, [&](const Live &live) { return live.untilUs <= nowUs; });
}

std::optional<proto::Mark> Pinger::Ping(proto::MarkKind kind, const proto::TargetRef &target,
                                        const proto::Vec3 &position, std::uint64_t nowUs) {
  if (kind == proto::MarkKind::None)
    return std::nullopt;
  Prune(nowUs);
  proto::Mark next{.kind = kind, .target = target, .position = position};
  for (auto it = m_live.begin(); it != m_live.end(); ++it) {
    if (it->mark.kind == proto::MarkKind::Target || !SameThing(it->mark, next))
      continue; // (a team target is not a ping: never toggled by one)
    if (nowUs - it->sentUs < kDangerWindowUs && it->mark.kind != proto::MarkKind::Danger) {
      // Twice in a row: careful. The same mark, now a danger.
      it->mark.kind = proto::MarkKind::Danger;
      it->mark.position = position;
      it->mark.seconds = LifeOf(proto::MarkKind::Danger, 0);
      it->sentUs = nowUs;
      it->untilUs = nowUs + std::uint64_t{it->mark.seconds} * 1'000'000;
      return it->mark;
    }
    if (nowUs - it->sentUs < kDangerWindowUs)
      return std::nullopt; // a third press in the window: nothing more to say
    // Our own mark pinged again: taken back.
    proto::Mark gone = it->mark;
    gone.kind = proto::MarkKind::None;
    m_live.erase(it);
    return gone;
  }
  if (!m_limiter.Allow(nowUs))
    return std::nullopt;
  next.id = ++m_next;
  next.seconds = LifeOf(kind, 0);
  m_live.push_back({next, nowUs, nowUs + std::uint64_t{next.seconds} * 1'000'000});
  // Our oldest goes past the receivers' cap, as it does on their boards.
  const auto ping = [](const Live &live) { return live.mark.kind != proto::MarkKind::Target; };
  if (static_cast<std::size_t>(std::ranges::count_if(m_live, ping)) > kMarksPerPlayer)
    m_live.erase(std::ranges::find_if(m_live, ping));
  return next;
}

std::optional<proto::Mark> Pinger::Target(const proto::TargetRef &target,
                                          const proto::Vec3 &position, std::uint64_t nowUs) {
  Prune(nowUs);
  const proto::Mark next{.kind = proto::MarkKind::Target, .target = target, .position = position};
  const auto live = std::ranges::find_if(
      m_live, [](const Live &l) { return l.mark.kind == proto::MarkKind::Target; });
  if (live != m_live.end() && SameThing(live->mark, next)) {
    // Chosen again on the same target: cleared.
    proto::Mark gone = live->mark;
    gone.kind = proto::MarkKind::None;
    m_live.erase(live);
    return gone;
  }
  if (!m_limiter.Allow(nowUs))
    return std::nullopt;
  if (live != m_live.end())
    m_live.erase(live); // replaced: the receivers drop it on the new one
  auto mark = next;
  mark.id = ++m_next;
  mark.seconds = LifeOf(proto::MarkKind::Target, 0);
  m_live.push_back({mark, nowUs, nowUs + std::uint64_t{mark.seconds} * 1'000'000});
  return mark;
}

std::optional<proto::Mark> Pinger::ClearTarget(std::uint64_t nowUs) {
  Prune(nowUs);
  const auto live = std::ranges::find_if(
      m_live, [](const Live &l) { return l.mark.kind == proto::MarkKind::Target; });
  if (live == m_live.end())
    return std::nullopt;
  proto::Mark gone = live->mark;
  gone.kind = proto::MarkKind::None;
  m_live.erase(live);
  return gone;
}

std::optional<proto::Mark> Pinger::Withdraw(std::uint32_t id, std::uint64_t nowUs) {
  Prune(nowUs);
  const auto it =
      std::ranges::find_if(m_live, [&](const Live &live) { return live.mark.id == id; });
  if (it == m_live.end())
    return std::nullopt;
  proto::Mark gone = it->mark;
  gone.kind = proto::MarkKind::None;
  m_live.erase(it);
  return gone;
}

std::vector<proto::Mark> Pinger::Alive(std::uint64_t nowUs) const {
  std::vector<proto::Mark> out;
  for (const auto &live : m_live)
    if (live.untilUs > nowUs)
      out.push_back(live.mark);
  return out;
}

std::vector<Change> Board::Add(const proto::Mark &mark, std::uint64_t nowUs) {
  std::vector<Change> out;
  const auto hide = [&](auto it) {
    out.push_back({Change::Kind::Hide, it->mark});
    return m_shown.erase(it);
  };
  // The same mark (replaced or withdrawn), the sender's older ping of the
  // same thing, or (a team target) its last team target: off first. Pings
  // and team targets never replace each other.
  const auto isTarget = [](const proto::Mark &m) { return m.kind == proto::MarkKind::Target; };
  const bool target = isTarget(mark);
  for (auto it = m_shown.begin(); it != m_shown.end();) {
    const bool sameSender = mark.kind != proto::MarkKind::None && it->mark.from == mark.from;
    if (SameMark(it->mark, mark) || (sameSender && target && isTarget(it->mark)) ||
        (sameSender && !target && !isTarget(it->mark) && SameThing(it->mark, mark)))
      it = hide(it);
    else
      ++it;
  }
  if (mark.kind == proto::MarkKind::None)
    return out;
  // At most kMarksPerPlayer pings of one sender (its team target apart):
  // its oldest goes.
  const auto ping = [&](const Shown &shown) {
    return shown.mark.from == mark.from && !isTarget(shown.mark);
  };
  if (!target &&
      std::ranges::count_if(m_shown, ping) >= static_cast<std::ptrdiff_t>(kMarksPerPlayer)) {
    const auto oldest = std::ranges::min_element(m_shown, [&](const Shown &a, const Shown &b) {
      const bool aOurs = ping(a), bOurs = ping(b);
      if (aOurs != bOurs)
        return aOurs;
      return a.sinceUs < b.sinceUs;
    });
    hide(oldest);
  }
  auto shown = mark;
  shown.seconds = LifeOf(mark.kind, mark.seconds);
  m_shown.push_back({shown, nowUs, nowUs + std::uint64_t{shown.seconds} * 1'000'000});
  out.push_back({Change::Kind::Show, shown});
  return out;
}

std::vector<Change> Board::Expire(std::uint64_t nowUs) {
  std::vector<Change> out;
  for (auto it = m_shown.begin(); it != m_shown.end();) {
    if (it->untilUs <= nowUs) {
      out.push_back({Change::Kind::Hide, it->mark});
      it = m_shown.erase(it);
    } else {
      ++it;
    }
  }
  return out;
}

std::vector<Change> Board::Forget(proto::PlayerId player) {
  std::vector<Change> out;
  for (auto it = m_shown.begin(); it != m_shown.end();) {
    if (it->mark.from == player) {
      out.push_back({Change::Kind::Hide, it->mark});
      it = m_shown.erase(it);
    } else {
      ++it;
    }
  }
  return out;
}

std::vector<Change> Board::Clear() {
  std::vector<Change> out;
  for (const auto &shown : m_shown)
    out.push_back({Change::Kind::Hide, shown.mark});
  m_shown.clear();
  return out;
}

bool Consents(const proto::Link *link, proto::PlayerId other) {
  if (!link || other == proto::kNoPlayer)
    return false;
  const bool listed = std::ranges::find(link->players, other) != link->players.end();
  switch (link->mode) {
  case proto::LinkMode::Listed:
    return listed;
  case proto::LinkMode::Everyone:
    return !listed;
  case proto::LinkMode::Off:
    break;
  }
  return false;
}

bool Linked(const std::map<proto::PlayerId, proto::Link> &links, proto::PlayerId a,
            proto::PlayerId b) {
  if (a == b)
    return false;
  const auto ofA = links.find(a), ofB = links.find(b);
  return Consents(ofA == links.end() ? nullptr : &ofA->second, b) &&
         Consents(ofB == links.end() ? nullptr : &ofB->second, a);
}

proto::Link WithConsent(proto::Link own, proto::PlayerId other, bool on) {
  if (own.mode == proto::LinkMode::Off || other == proto::kNoPlayer)
    return own; // link = off: never
  auto &list = own.players;
  const auto it = std::ranges::find(list, other);
  // Listed: the list lets in; Everyone: it leaves out.
  const bool add = (own.mode == proto::LinkMode::Listed) == on;
  if (add && it == list.end() && list.size() < proto::kMaxLinkPlayers)
    list.push_back(other);
  else if (!add && it != list.end())
    list.erase(it);
  return own;
}

LinkState Links::StateOf(proto::PlayerId player) const {
  if (!m_players.contains(player))
    return LinkState::None;
  const bool ours = Consents(&m_own, player);
  const auto it = m_theirs.find(player);
  const bool theirs = it != m_theirs.end() && Consents(&it->second, m_you);
  if (ours && theirs)
    return LinkState::Linked;
  if (ours)
    return LinkState::Asking;
  return theirs ? LinkState::Offered : LinkState::None;
}

std::map<proto::PlayerId, LinkState> Links::Snapshot() const {
  std::map<proto::PlayerId, LinkState> out;
  for (const auto player : m_players)
    out[player] = StateOf(player);
  return out;
}

std::vector<LinkChange> Links::Diff(const std::map<proto::PlayerId, LinkState> &before) const {
  std::vector<LinkChange> out;
  std::set<proto::PlayerId> everyone = m_players;
  for (const auto &[player, state] : before)
    everyone.insert(player);
  for (const auto player : everyone) {
    const auto was = before.find(player);
    const auto from = was == before.end() ? LinkState::None : was->second;
    const auto to = StateOf(player);
    if (from != to)
      out.push_back({player, from, to});
  }
  return out;
}

std::vector<LinkChange> Links::SetOwn(const proto::Link &own) {
  const auto before = Snapshot();
  m_own = own;
  return Diff(before);
}

std::vector<LinkChange> Links::Note(const proto::Link &theirs) {
  if (theirs.player == proto::kNoPlayer || theirs.player == m_you)
    return {};
  const auto before = Snapshot();
  m_theirs[theirs.player] = theirs;
  return Diff(before);
}

std::vector<LinkChange> Links::Join(proto::PlayerId player) {
  if (player == proto::kNoPlayer || player == m_you)
    return {};
  const auto before = Snapshot();
  m_players.insert(player);
  return Diff(before);
}

std::vector<LinkChange> Links::Forget(proto::PlayerId player) {
  const auto before = Snapshot();
  m_players.erase(player);
  m_theirs.erase(player);
  return Diff(before);
}

void Links::Clear() {
  m_players.clear();
  m_theirs.clear();
  m_you = proto::kNoPlayer;
}

std::vector<TagChange> TagLedger::Release(const Key &key, proto::PlayerId player) {
  const auto it = m_held.find(key);
  if (it == m_held.end() || it->second.erase(player) == 0 || !it->second.empty())
    return {};
  m_held.erase(it);
  return {{.on = false, .reason = key.reason, .target = key.target}};
}

std::vector<TagChange> TagLedger::Apply(const proto::Tag &tag, std::uint64_t nowUs) {
  std::vector<TagChange> out;
  for (const auto &target : tag.targets) {
    if (target.kind == proto::TargetKind::None || target.kind == proto::TargetKind::Player)
      continue; // nothing to outline
    const Key key{tag.reason, target};
    if (!tag.on) {
      const auto off = Release(key, tag.from);
      out.insert(out.end(), off.begin(), off.end());
      continue;
    }
    const auto seconds =
        tag.reason == proto::TagReason::Reveal
            ? std::uint8_t(tag.seconds == 0 ? kMaxRevealSeconds
                                            : std::min(tag.seconds, kMaxRevealSeconds))
            : std::uint8_t{0};
    const std::uint64_t until = seconds ? nowUs + std::uint64_t{seconds} * 1'000'000 : 0;
    auto it = m_held.find(key);
    if (it == m_held.end()) {
      if (m_held.size() >= kMaxHeld)
        continue; // full: a flood is not shown
      it = m_held.emplace(key, std::map<proto::PlayerId, std::uint64_t>{}).first;
      out.push_back({.on = true,
                     .reason = tag.reason,
                     .target = target,
                     .seconds = static_cast<float>(seconds)});
    }
    // Held already (by them or another): a longer hold, nothing to redraw.
    // (A scan's hold has no end, a reveal's always one: reasons never mix.)
    auto &held = it->second[tag.from];
    held = until == 0 ? 0 : std::max(held, until);
  }
  return out;
}

std::vector<TagChange> TagLedger::Expire(std::uint64_t nowUs) {
  std::vector<TagChange> out;
  for (auto it = m_held.begin(); it != m_held.end();) {
    std::erase_if(it->second,
                  [&](const auto &entry) { return entry.second != 0 && entry.second <= nowUs; });
    if (it->second.empty()) {
      out.push_back({.on = false, .reason = it->first.reason, .target = it->first.target});
      it = m_held.erase(it);
    } else {
      ++it;
    }
  }
  return out;
}

std::vector<TagChange> TagLedger::Forget(proto::PlayerId player) {
  std::vector<TagChange> out;
  for (auto it = m_held.begin(); it != m_held.end();) {
    if (it->second.erase(player) > 0 && it->second.empty()) {
      out.push_back({.on = false, .reason = it->first.reason, .target = it->first.target});
      it = m_held.erase(it);
    } else {
      ++it;
    }
  }
  return out;
}

std::vector<TagChange> TagLedger::Clear() {
  std::vector<TagChange> out;
  for (const auto &[key, holders] : m_held)
    out.push_back({.on = false, .reason = key.reason, .target = key.target});
  m_held.clear();
  return out;
}

bool Moved(const proto::Waypoint &a, const proto::Waypoint &b) {
  if (a.kind != b.kind)
    return true;
  return a.kind != proto::WaypointKind::None && Distance(a.position, b.position) > 1.f;
}

HackChange HackBoard::ChangeOf(const Running &running, bool show, std::uint64_t nowUs) const {
  using K = HackChange::Kind;
  HackChange change{.kind = running.upload ? (show ? K::ShowUpload : K::HideUpload)
                                           : (show ? K::ShowEffect : K::HideEffect),
                    .target = running.target,
                    .player = running.player,
                    .action = running.action,
                    .effect = running.effect};
  if (show && running.untilUs > nowUs)
    change.seconds = static_cast<float>(running.untilUs - nowUs) / 1e6f;
  return change;
}

std::size_t HackBoard::Shown() const {
  return static_cast<std::size_t>(std::ranges::count_if(
      m_running, [&](const Running &running) { return m_linked.contains(running.player); }));
}

std::vector<HackChange> HackBoard::Upload(const proto::TargetRef &target, proto::PlayerId player,
                                          std::uint64_t action, float seconds,
                                          std::uint64_t nowUs) {
  std::vector<HackChange> out;
  if (player == proto::kNoPlayer)
    return out;
  m_lastAction[KeyOf(target, player)] = action;
  // One upload of a player on a target at a time on screen (the game's own
  // indicator shows one too): a new one replaces it.
  for (auto it = m_running.begin(); it != m_running.end();)
    if (it->upload && it->target == target && it->player == player) {
      if (m_linked.contains(player))
        out.push_back(ChangeOf(*it, false, nowUs));
      it = m_running.erase(it);
    } else {
      ++it;
    }
  if (m_running.size() >= kMaxShown)
    return out;
  const auto length = static_cast<std::uint64_t>(std::max(seconds, 0.f) * 1e6f);
  // A second more than its length: the end the host says comes first.
  const Running running{target, player, true, action, 0, nowUs + length + 1'000'000};
  m_running.push_back(running);
  if (m_linked.contains(player)) {
    auto shown = ChangeOf(running, true, nowUs);
    shown.seconds = std::max(seconds, 0.f);
    out.push_back(shown);
  }
  return out;
}

std::vector<HackChange> HackBoard::UploadEnded(const proto::TargetRef &target,
                                               proto::PlayerId player, std::uint64_t action) {
  std::vector<HackChange> out;
  for (auto it = m_running.begin(); it != m_running.end();)
    if (it->upload && it->target == target && it->player == player &&
        (action == 0 || it->action == action)) {
      if (m_linked.contains(player))
        out.push_back(ChangeOf(*it, false, 0));
      it = m_running.erase(it);
    } else {
      ++it;
    }
  return out;
}

std::vector<HackChange> HackBoard::Hide(const proto::TargetRef &target, proto::PlayerId player,
                                        std::uint64_t effect, std::uint64_t nowUs) {
  std::vector<HackChange> out;
  for (auto it = m_running.begin(); it != m_running.end();)
    if (!it->upload && it->target == target && it->effect == effect &&
        (player == proto::kNoPlayer || it->player == player)) {
      if (m_linked.contains(it->player))
        out.push_back(ChangeOf(*it, false, nowUs));
      it = m_running.erase(it);
    } else {
      ++it;
    }
  return out;
}

std::vector<HackChange> HackBoard::Show(const proto::TargetRef &target, proto::PlayerId player,
                                        std::uint64_t action, std::uint64_t effect, float seconds,
                                        std::uint64_t nowUs) {
  // The same effect again (refreshed): replaced.
  auto out = Hide(target, player, effect, nowUs);
  if (m_running.size() >= kMaxShown)
    return out;
  const std::uint64_t length =
      seconds > 0.f ? static_cast<std::uint64_t>(seconds * 1e6f) : kUnknownEffectUs;
  const Running running{target, player, false, action, effect, nowUs + length};
  m_running.push_back(running);
  if (m_linked.contains(player)) {
    auto shown = ChangeOf(running, true, nowUs);
    shown.seconds = std::max(seconds, 0.f);
    out.push_back(shown);
  }
  return out;
}

std::vector<HackChange> HackBoard::Effect(const proto::TargetRef &target, proto::PlayerId player,
                                          std::uint64_t effect, bool applied, float seconds,
                                          std::uint64_t nowUs) {
  if (!applied)
    return Hide(target, player, effect, nowUs);
  if (player == proto::kNoPlayer)
    return {}; // an NPC's own, or nobody's: not a partner's hack
  const auto last = m_lastAction.find(KeyOf(target, player));
  return Show(target, player, last == m_lastAction.end() ? 0 : last->second, effect, seconds,
              nowUs);
}

std::vector<HackChange> HackBoard::Device(std::uint64_t entity, proto::PlayerId player,
                                          std::uint16_t before, std::uint16_t after,
                                          const DevicePrograms &programs, std::uint64_t nowUs) {
  std::vector<HackChange> out;
  const auto target = DeviceTarget(entity);
  const auto append = [&](const std::vector<HackChange> &more) {
    out.insert(out.end(), more.begin(), more.end());
  };
  const auto on = [&](std::uint16_t bit) { return (after & bit) != 0 && (before & bit) == 0; };
  const auto off = [&](std::uint16_t bit) { return (before & bit) != 0 && (after & bit) == 0; };
  // What went off, whoever's it was.
  if (off(proto::kDeviceGlitching))
    append(Hide(target, proto::kNoPlayer, kDeviceGlitchEffect, nowUs));
  if (off(proto::kDeviceFriendly))
    append(Hide(target, proto::kNoPlayer, kDeviceFriendlyEffect, nowUs));
  if (off(proto::kDeviceHacked))
    append(Hide(target, proto::kNoPlayer, kDeviceHackedEffect, nowUs));
  if (player == proto::kNoPlayer)
    return out;
  // What came on: what lasts shows while it does; a quickhack that left
  // nothing to see, a moment.
  bool lasting = false;
  if (on(proto::kDeviceGlitching)) {
    append(Show(target, player, programs.glitch, kDeviceGlitchEffect, 0.f, nowUs));
    lasting = true;
  }
  if (on(proto::kDeviceFriendly)) {
    append(Show(target, player, programs.friendly, kDeviceFriendlyEffect, 0.f, nowUs));
    lasting = true;
  }
  if (on(proto::kDeviceHacked) && !lasting)
    append(Show(target, player, programs.hacked, kDeviceHackedEffect, kDeviceHackSeconds, nowUs));
  return out;
}

std::vector<HackChange> HackBoard::ForgetTarget(const proto::TargetRef &target) {
  std::vector<HackChange> out;
  for (auto it = m_running.begin(); it != m_running.end();)
    if (it->target == target) {
      if (m_linked.contains(it->player))
        out.push_back(ChangeOf(*it, false, 0));
      it = m_running.erase(it);
    } else {
      ++it;
    }
  std::erase_if(m_lastAction, [&](const auto &entry) {
    return entry.first.first == std::pair{target.kind, target.id};
  });
  return out;
}

std::vector<HackChange> HackBoard::SetLinked(proto::PlayerId player, bool linked,
                                             std::uint64_t nowUs) {
  std::vector<HackChange> out;
  if (linked == m_linked.contains(player))
    return out;
  if (linked)
    m_linked.insert(player);
  else
    m_linked.erase(player);
  for (const auto &running : m_running)
    if (running.player == player && running.untilUs > nowUs)
      out.push_back(ChangeOf(running, linked, nowUs));
  return out;
}

std::vector<HackChange> HackBoard::Forget(proto::PlayerId player) {
  std::vector<HackChange> out;
  for (auto it = m_running.begin(); it != m_running.end();)
    if (it->player == player) {
      if (m_linked.contains(player))
        out.push_back(ChangeOf(*it, false, 0));
      it = m_running.erase(it);
    } else {
      ++it;
    }
  m_linked.erase(player);
  std::erase_if(m_lastAction, [&](const auto &entry) { return entry.first.second == player; });
  return out;
}

std::vector<HackChange> HackBoard::Expire(std::uint64_t nowUs) {
  std::vector<HackChange> out;
  for (auto it = m_running.begin(); it != m_running.end();)
    if (it->untilUs <= nowUs) {
      if (m_linked.contains(it->player))
        out.push_back(ChangeOf(*it, false, nowUs));
      it = m_running.erase(it);
    } else {
      ++it;
    }
  return out;
}

std::vector<HackChange> HackBoard::Clear() {
  std::vector<HackChange> out;
  for (const auto &running : m_running)
    if (m_linked.contains(running.player))
      out.push_back(ChangeOf(running, false, 0));
  m_running.clear();
  m_linked.clear();
  m_lastAction.clear();
  return out;
}
} // namespace tandem::comms
