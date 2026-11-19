#include "comms.hpp"

#include "log.hpp"

#include <format>

namespace tandem::plugin {
Comms &Comms::Get() {
  static Comms comms;
  return comms;
}

void Comms::Configure(LinkDefault link, bool pings) {
  std::lock_guard _(m_lock);
  m_linkDefault = link;
  m_pings = pings;
  m_own.mode = link == LinkDefault::Auto  ? proto::LinkMode::Everyone
               : link == LinkDefault::Ask ? proto::LinkMode::Listed
                                          : proto::LinkMode::Off;
  m_own.players.clear();
  m_links.SetOwn(m_own);
}

void Comms::PushInbound(CommsInbound inbound) {
  std::lock_guard _(m_lock);
  // Drained every tick; a pile-up means the game is not ticking (loading).
  if (m_inbound.size() < 256 || inbound.kind != CommsInbound::Kind::Message)
    m_inbound.push_back(std::move(inbound));
}

std::vector<proto::Message> Comms::TakeOutbound() {
  std::lock_guard _(m_lock);
  return std::exchange(m_outbound, {});
}

proto::Link Comms::OwnLink() {
  std::lock_guard _(m_lock);
  return m_own;
}

std::optional<proto::Waypoint> Comms::OwnWaypoint() {
  std::lock_guard _(m_lock);
  return m_waypoint;
}

void Comms::Queue(const std::vector<comms::Change> &changes) {
  for (const auto &change : changes)
    m_events.push_back({.kind = change.kind == comms::Change::Kind::Show
                                    ? CommsEvent::Kind::ShowMark
                                    : CommsEvent::Kind::HideMark,
                        .player = change.mark.from,
                        .id = change.mark.id,
                        .type = static_cast<std::int32_t>(change.mark.kind),
                        .target = change.mark.target,
                        .position = change.mark.position,
                        .seconds = static_cast<float>(change.mark.seconds)});
}

void Comms::Queue(const std::vector<comms::TagChange> &changes) {
  for (const auto &change : changes)
    m_events.push_back({.kind = change.on ? CommsEvent::Kind::TagOn : CommsEvent::Kind::TagOff,
                        .type = static_cast<std::int32_t>(change.reason),
                        .target = change.target,
                        .seconds = change.seconds});
}

void Comms::Queue(const std::vector<comms::LinkChange> &changes) {
  for (const auto &change : changes) {
    log::Info("comms: link with player {}: {} -> {}", change.player,
              static_cast<int>(change.before), static_cast<int>(change.after));
    m_events.push_back({.kind = CommsEvent::Kind::Link,
                        .player = change.player,
                        .type = static_cast<std::int32_t>(change.after),
                        .before = static_cast<std::int32_t>(change.before)});
    // No longer linked: what they had outlined here goes.
    if (change.before == comms::LinkState::Linked)
      Queue(m_tags.Forget(change.player));
  }
}

void Comms::Apply(const proto::Message &message, std::uint64_t nowUs) {
  if (const auto *mark = std::get_if<proto::Mark>(&message)) {
    if (!m_pings || mark->from == m_you)
      return;
    ++m_heardMarks;
    Queue(m_board.Add(*mark, nowUs));
  } else if (const auto *tag = std::get_if<proto::Tag>(&message)) {
    if (!m_links.LinkedWith(tag->from))
      return; // broken on the way
    ++m_heardTags;
    Queue(m_tags.Apply(*tag, nowUs));
  } else if (const auto *link = std::get_if<proto::Link>(&message)) {
    Queue(m_links.Note(*link));
  } else if (const auto *waypoint = std::get_if<proto::Waypoint>(&message)) {
    if (waypoint->player == m_you)
      return;
    if (waypoint->kind == proto::WaypointKind::None) {
      if (m_waypointsShown.erase(waypoint->player) == 0)
        return;
    } else {
      m_waypointsShown[waypoint->player] = waypoint->kind;
    }
    m_events.push_back({.kind = CommsEvent::Kind::Waypoint,
                        .player = waypoint->player,
                        .type = static_cast<std::int32_t>(waypoint->kind),
                        .position = waypoint->position});
  }
}

std::vector<CommsEvent> Comms::Update(std::uint64_t nowUs) {
  std::lock_guard _(m_lock);
  for (auto &inbound : std::exchange(m_inbound, {})) {
    switch (inbound.kind) {
    case CommsInbound::Kind::InSession:
      m_you = inbound.player;
      m_links.SetYou(m_you);
      break;
    case CommsInbound::Kind::Joined:
      Queue(m_links.Join(inbound.player));
      break;
    case CommsInbound::Kind::Left:
      // A player can vanish at any moment: nothing of theirs stays.
      Queue(m_board.Forget(inbound.player));
      Queue(m_links.Forget(inbound.player));
      Queue(m_tags.Forget(inbound.player));
      if (m_waypointsShown.erase(inbound.player) > 0)
        m_events.push_back({.kind = CommsEvent::Kind::Waypoint, .player = inbound.player});
      break;
    case CommsInbound::Kind::Ended: {
      Queue(m_board.Clear());
      Queue(m_tags.Clear());
      for (const auto player : std::vector(m_links.Players().begin(), m_links.Players().end()))
        Queue(m_links.Forget(player));
      m_links.Clear();
      for (const auto &[player, kind] : m_waypointsShown)
        m_events.push_back({.kind = CommsEvent::Kind::Waypoint, .player = player});
      m_waypointsShown.clear();
      m_pinger.Clear();
      m_pendingTags.clear();
      // What the next session needs of us it gets from OwnLink and
      // OwnWaypoint at its start: nothing queued for this one is sent there.
      m_outbound.clear();
      m_you = proto::kNoPlayer;
      break;
    }
    case CommsInbound::Kind::Message:
      Apply(inbound.message, nowUs);
      break;
    }
  }
  Queue(m_board.Expire(nowUs));
  Queue(m_tags.Expire(nowUs));
  FlushTags();
  return std::exchange(m_events, {});
}

std::int32_t Comms::Ping(proto::MarkKind kind, const proto::TargetRef &target,
                         const proto::Vec3 &position, std::uint64_t nowUs) {
  std::lock_guard _(m_lock);
  if (!m_pings || m_you == proto::kNoPlayer)
    return 0;
  auto mark = m_pinger.Ping(kind, target, position, nowUs);
  if (!mark)
    return 0;
  mark->from = m_you;
  ++m_sentMarks;
  m_outbound.push_back(*mark);
  // Ours shows here too, as the others see it.
  Queue(m_board.Add(*mark, nowUs));
  if (mark->kind == proto::MarkKind::None)
    return 3;
  return mark->kind == proto::MarkKind::Danger && kind != proto::MarkKind::Danger ? 2 : 1;
}

void Comms::Tag(proto::TagReason reason, bool on, std::uint8_t seconds,
                const proto::TargetRef &target) {
  std::lock_guard _(m_lock);
  if (m_you == proto::kNoPlayer || target.kind == proto::TargetKind::None)
    return;
  for (auto &pending : m_pendingTags)
    if (pending.reason == reason && pending.on == on && pending.seconds == seconds &&
        pending.targets.size() < proto::kMaxTagTargets) {
      if (std::ranges::find(pending.targets, target) == pending.targets.end())
        pending.targets.push_back(target);
      return;
    }
  if (m_pendingTags.size() < 8)
    m_pendingTags.push_back({.reason = reason, .on = on, .seconds = seconds, .targets = {target}});
}

void Comms::FlushTags() {
  // Nobody linked: nothing to say (the host would deliver it to nobody).
  bool linked = false;
  for (const auto player : m_links.Players())
    linked = linked || m_links.LinkedWith(player);
  for (auto &tag : std::exchange(m_pendingTags, {}))
    if (linked) {
      ++m_sentTags;
      m_outbound.push_back(std::move(tag));
    }
}

bool Comms::SetConsent(proto::PlayerId player, bool on) {
  std::lock_guard _(m_lock);
  if (m_own.mode == proto::LinkMode::Off || player == m_you || player == proto::kNoPlayer)
    return false;
  const auto next = comms::WithConsent(m_own, player, on);
  if (next == m_own)
    return true;
  m_own = next;
  m_outbound.push_back(m_own);
  Queue(m_links.SetOwn(m_own));
  return true;
}

comms::LinkState Comms::StateOf(proto::PlayerId player) {
  std::lock_guard _(m_lock);
  return m_links.StateOf(player);
}

bool Comms::AnyLinked() {
  std::lock_guard _(m_lock);
  for (const auto player : m_links.Players())
    if (m_links.LinkedWith(player))
      return true;
  return false;
}

void Comms::SetWaypoint(proto::WaypointKind kind, const proto::Vec3 &position) {
  std::lock_guard _(m_lock);
  const proto::Waypoint next{.kind = kind, .position = position};
  if (!comms::Moved(m_waypoint.value_or(proto::Waypoint{}), next))
    return;
  m_waypoint = next;
  m_outbound.push_back(next);
}

std::string Comms::Report() {
  std::lock_guard _(m_lock);
  std::string links;
  for (const auto player : m_links.Players())
    links += std::format(" {}:{}", player, static_cast<int>(m_links.StateOf(player)));
  return std::format("comms: marks sent {}, heard {}, on show {}; tags sent {}, heard {}, "
                     "outlined {}; own link mode {} ({} listed); links{}; waypoints shown {}",
                     m_sentMarks, m_heardMarks, m_board.Size(), m_sentTags, m_heardTags,
                     m_tags.Size(), static_cast<int>(m_own.mode), m_own.players.size(),
                     links.empty() ? " none" : links, m_waypointsShown.size());
}
} // namespace tandem::plugin
