// Physics props in the session (docs/architecture/props.md): a placed prop
// with a physics body that something moved is simulated by the game that
// runs the area round it (AreaOwner), which says how it moves and where it
// rests; the players linked with that game hold their own instance of the
// prop to its word. One game speaks for a moving prop at a time, first word
// first (props::Speakers, the host's). The host keeps where moved props came
// to rest (the session's record: late joiners, players who come later, the
// world digest) and routes a push one game's V gave a prop to the game that
// runs it.

#include "tandem/endpoint.hpp"

#include <cmath>

namespace tandem {
namespace {
bool Finite(const proto::Vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool Finite(const proto::Quat &q) {
  return std::isfinite(q.i) && std::isfinite(q.j) && std::isfinite(q.k) && std::isfinite(q.r);
}
} // namespace

bool Endpoint::SendPropStates(proto::PropStates states) {
  if (!IsJoined() || m_ended || !proto::Fits(states))
    return false;
  std::erase_if(states.props, [](const proto::PropEntry &prop) { return !Finite(prop.position); });
  if (states.props.empty())
    return false;
  states.owner = m_you;
  states.timeUs = HostNowUs();
  // The host's own word goes through its arbitration as a guest's does.
  if (IsHost())
    return OnPropStates(std::move(states), std::nullopt, nullptr);
  if (!OwnsArea())
    return false;
  // A prop another game speaks for is that game's to say.
  const auto now = net::NowUs();
  std::erase_if(states.props, [&](const proto::PropEntry &prop) {
    const auto speaker = m_propSpeakers.Of({prop.entity, prop.part}, now);
    return speaker && *speaker != m_you;
  });
  if (states.props.empty())
    return false;
  for (const auto &prop : states.props)
    m_propSpeakers.Heard({prop.entity, prop.part}, m_you, now);
  Send(m_hostPeer, states);
  return true;
}

bool Endpoint::SendPropRest(proto::PropRest rest) {
  if (!IsJoined() || m_ended || rest.entity == 0 || !Finite(rest.position) ||
      !Finite(rest.orientation))
    return false;
  const auto now = net::NowUs();
  const auto speaker = m_propSpeakers.Of({rest.entity, rest.part}, now);
  if (speaker && *speaker != m_you)
    return false;
  rest.owner = m_you;
  if (IsHost()) {
    const auto before = m_propsRefused;
    OnPropRest(rest, nullptr);
    return m_propsRefused == before;
  }
  // Ours to finish while we speak for it, even once our area went to
  // another game; a new one needs our area.
  if (!speaker && !OwnsArea())
    return false;
  m_propSpeakers.Heard({rest.entity, rest.part}, m_you, now);
  Send(m_hostPeer, rest);
  return true;
}

void Endpoint::SendPropPush(proto::PropPush push) {
  if (!IsJoined() || m_ended || push.entity == 0 || !Finite(push.position) ||
      !Finite(push.velocity))
    return;
  push.player = m_you;
  push.timeUs = HostNowUs();
  if (IsHost())
    OnPropPush(push, nullptr);
  else
    Send(m_hostPeer, push);
}

std::optional<proto::PlayerId> Endpoint::PropSpeaker(std::uint64_t entity,
                                                     std::uint8_t part) const {
  return m_propSpeakers.Of({entity, part}, net::NowUs());
}

bool Endpoint::OnPropStates(proto::PropStates states, std::optional<net::PeerId> peer,
                            std::vector<SessionEvent> *out) {
  const auto now = net::NowUs();
  const auto player = states.owner;
  const bool ownsArea = AreaOwner(player) == player;
  m_propSpeakers.Expire(now);
  const auto before = states.props.size();
  std::erase_if(states.props, [&](const proto::PropEntry &prop) {
    return !Finite(prop.position) ||
           !m_propSpeakers.Claim({prop.entity, prop.part}, player, now, ownsArea);
  });
  m_propsRefused += before - states.props.size();
  if (states.props.empty())
    return false;
  // For the players linked with its game, as its NPCs' states.
  ShareNpc(states, player, peer.value_or(0));
  if (out && player != m_you && Sees(m_you, player))
    out->push_back(events::PropsMoved{std::move(states)});
  return true;
}

void Endpoint::OnPropRest(proto::PropRest rest, std::vector<SessionEvent> *out) {
  const auto now = net::NowUs();
  const auto player = rest.owner;
  if (!Finite(rest.position) || !Finite(rest.orientation) ||
      !m_propSpeakers.Claim({rest.entity, rest.part}, player, now, AreaOwner(player) == player)) {
    ++m_propsRefused;
    return;
  }
  const bool known = m_propRecord.Knows({rest.entity, rest.part});
  if (!m_propRecord.Note(rest)) {
    // The same rest again, or a new prop past a full record.
    if (!known)
      ++m_propsRefused;
    return;
  }
  // To everyone, its teller too (for its record): one away from it puts the
  // prop there when it comes.
  Broadcast(rest);
  if (out && player != m_you)
    out->push_back(events::PropRested{rest});
}

void Endpoint::OnPropPush(proto::PropPush push, std::vector<SessionEvent> *out) {
  const auto now = HostNowUs();
  if (now > push.timeUs && now - push.timeUs > proto::kPropPushMaxAgeUs) {
    ++m_propsRefused;
    return;
  }
  // Its speaker, while one speaks; else the game that runs the pusher's area.
  auto to = AreaOwner(push.player);
  if (const auto speaker = m_propSpeakers.Of({push.entity, push.part}, net::NowUs()))
    to = *speaker;
  if (to == push.player) {
    // Its own to simulate: nobody else's to give.
    ++m_propsRefused;
    return;
  }
  if (RouteToPlayer(to, push, push.player) && out)
    out->push_back(events::PropPushed{push});
}

bool Endpoint::OnGuestProps(net::PeerId peer, proto::PlayerId player, const proto::Message &message,
                            std::vector<SessionEvent> &out) {
  if (const auto *states = std::get_if<proto::PropStates>(&message)) {
    // Who says is the connection, never the message.
    auto mine = *states;
    mine.owner = player;
    OnPropStates(std::move(mine), peer, &out);
    return true;
  }
  if (const auto *rest = std::get_if<proto::PropRest>(&message)) {
    auto mine = *rest;
    mine.owner = player;
    OnPropRest(mine, &out);
    return true;
  }
  if (const auto *push = std::get_if<proto::PropPush>(&message)) {
    auto mine = *push;
    mine.player = player;
    OnPropPush(mine, &out);
    return true;
  }
  return false;
}

bool Endpoint::OnHostProps(const proto::Message &message, std::vector<SessionEvent> &out) {
  const auto now = net::NowUs();
  if (const auto *states = std::get_if<proto::PropStates>(&message)) {
    // Another game's word (never our own back): it speaks for these, and
    // ours stays silent about them.
    if (states->owner == m_you || states->owner == proto::kNoPlayer)
      return true;
    for (const auto &prop : states->props)
      m_propSpeakers.Heard({prop.entity, prop.part}, states->owner, now);
    out.push_back(events::PropsMoved{*states});
    return true;
  }
  if (const auto *rest = std::get_if<proto::PropRest>(&message)) {
    // Kept as the host sent it (our own back too: the record is the
    // session's); shown unless it is our own.
    if (!m_propRecord.Note(*rest))
      return true;
    if (rest->owner != m_you) {
      m_propSpeakers.Heard({rest->entity, rest->part}, rest->owner, now);
      out.push_back(events::PropRested{*rest});
    }
    return true;
  }
  if (const auto *push = std::get_if<proto::PropPush>(&message)) {
    if (push->player != m_you)
      out.push_back(events::PropPushed{*push});
    return true;
  }
  return false;
}
} // namespace tandem
