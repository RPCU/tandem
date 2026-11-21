// A shared campaign in the session (docs/architecture/campaign.md): each
// player's copy of the campaign, said once and when it changes, kept by the
// host for late joiners and passed on to everyone. The lineage rules are
// campaign.hpp's; the game decides what to offer from them.

#include "tandem/endpoint.hpp"

#include <utility>

namespace tandem {
void Endpoint::SetCampaign(proto::Campaign ours) {
  ours.player = m_you;
  if (m_campaign && *m_campaign == ours)
    return;
  m_campaign = ours;
  if (!IsJoined() || m_ended)
    return;
  if (IsHost())
    Broadcast(ours);
  else
    Send(m_hostPeer, ours);
}

void Endpoint::SendCampaignsTo(net::PeerId peer, proto::PlayerId to) {
  if (m_campaign) {
    auto ours = *m_campaign;
    ours.player = m_you;
    Send(peer, ours);
  }
  // Host: everyone else's too (a guest hears the others only through us).
  if (IsHost())
    for (const auto &[player, campaign] : m_campaigns)
      if (player != to)
        Send(peer, campaign);
}

void Endpoint::OnCampaign(proto::PlayerId player, proto::Campaign campaign,
                          std::vector<SessionEvent> &out) {
  // A guest speaks for itself only; the host stamps it. Ours never comes back.
  if (IsHost())
    campaign.player = player;
  if (campaign.player == proto::kNoPlayer || campaign.player == m_you)
    return;
  const auto known = m_campaigns.find(campaign.player);
  if (known != m_campaigns.end() && known->second == campaign)
    return;
  m_campaigns[campaign.player] = campaign;
  if (IsHost())
    if (const auto from = PeerOf(player))
      Broadcast(campaign, *from);
  const auto info = m_players.find(campaign.player);
  out.push_back(events::RemoteCampaign{
      campaign.player, info != m_players.end() ? info->second.name : std::string(), campaign});
}
} // namespace tandem
