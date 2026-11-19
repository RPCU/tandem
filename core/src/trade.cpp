#include "tandem/trade.hpp"

#include <algorithm>

namespace tandem::trade {
namespace {
constexpr std::size_t kOverKept = 256;
} // namespace

bool Valid(const proto::TradeOffer &offer) {
  if (offer.items.size() > proto::kMaxTradeItems || offer.money > proto::kMaxTradeMoney ||
      offer.asks > proto::kMaxTradeMoney)
    return false;
  for (std::size_t i = 0; i < offer.items.size(); ++i) {
    const auto &item = offer.items[i];
    if (item.item.record == 0 || item.item.quantity == 0 ||
        item.stats.size() > proto::kMaxTradeStats || item.parts.size() > proto::kMaxTradeParts)
      return false;
    // One line per stack: the same item twice would be given twice.
    for (std::size_t j = 0; j < i; ++j)
      if (offer.items[j].item.record == item.item.record &&
          offer.items[j].item.seed == item.item.seed)
        return false;
  }
  return true;
}

bool Empty(const proto::TradeOffer &offer) { return offer.items.empty() && offer.money == 0; }

int Table::Side(const Deal &deal, proto::PlayerId player) {
  return deal.party[0] == player ? 0 : deal.party[1] == player ? 1 : -1;
}

Out Table::Say(const Deal &deal, int toSide, proto::TradeVerb verb, proto::TradeReason reason) {
  return {deal.party[toSide], proto::Trade{.id = deal.id,
                                           .from = deal.party[1 - toSide],
                                           .to = deal.party[toSide],
                                           .verb = verb,
                                           .reason = reason}};
}

Out Table::Over(proto::TradeId id, proto::PlayerId to) {
  return {to, proto::Trade{.id = id,
                           .from = proto::kNoPlayer,
                           .to = to,
                           .verb = proto::TradeVerb::Close,
                           .reason = proto::TradeReason::Over}};
}

bool Table::IsOver(proto::TradeId id) const {
  return std::ranges::find(m_over, id) != m_over.end();
}

void Table::Finish(proto::TradeId id) {
  if (m_deals.erase(id) == 0)
    return;
  m_over.push_back(id);
  if (m_over.size() > kOverKept)
    m_over.pop_front();
}

bool Table::Busy(proto::PlayerId player) const {
  for (const auto &[id, deal] : m_deals)
    if (const int side = Side(deal, player);
        side >= 0 && !(deal.state == State::Committed && deal.applied[side]))
      return true;
  return false;
}

std::vector<Out> Table::On(proto::PlayerId from, const proto::Trade &trade, std::uint64_t nowUs,
                           const std::set<proto::PlayerId> &present) {
  using proto::TradeReason;
  using proto::TradeVerb;
  std::vector<Out> out;
  const auto refuse = [&](TradeReason reason) {
    out.push_back({from, proto::Trade{.id = trade.id,
                                      .from = trade.to,
                                      .to = from,
                                      .verb = TradeVerb::Close,
                                      .reason = reason}});
  };
  const auto it = m_deals.find(trade.id);

  if (trade.verb == TradeVerb::Request) {
    // Said again (the same trade), or about one that is over: not a new one.
    if (it != m_deals.end() || IsOver(trade.id))
      return out;
    if (proto::TradeAsker(trade.id) != from || trade.to == from || trade.to == proto::kNoPlayer)
      refuse(TradeReason::Invalid);
    else if (!present.contains(trade.to))
      refuse(TradeReason::Gone);
    else if (Busy(from) || Busy(trade.to))
      refuse(TradeReason::Busy);
    else {
      Deal deal{.id = trade.id, .party = {from, trade.to}, .sinceUs = nowUs};
      deal.offers[0] = {.id = trade.id, .player = from};
      deal.offers[1] = {.id = trade.id, .player = trade.to};
      out.push_back(Say(deal, 1, TradeVerb::Request, TradeReason::None));
      m_deals.emplace(trade.id, std::move(deal));
    }
    return out;
  }

  const int side = it == m_deals.end() ? -1 : Side(it->second, from);
  if (side < 0) {
    // No such trade (any more), or not this player's: whoever still thinks
    // it is at that table is told it is over. A close or an Applied about
    // it needs no answer.
    if (trade.verb != TradeVerb::Close && trade.verb != TradeVerb::Applied)
      out.push_back(Over(trade.id, from));
    return out;
  }
  auto &deal = it->second;
  const int other = 1 - side;

  switch (trade.verb) {
  case TradeVerb::Request:
    break;
  case TradeVerb::Accept:
    // Only the one asked accepts; again is nothing new.
    if (deal.state == State::Asked && side == 1) {
      deal.state = State::Open;
      deal.sinceUs = nowUs;
      out.push_back(Say(deal, 0, TradeVerb::Accept, TradeReason::None));
    }
    break;
  case TradeVerb::Close: {
    // After the commit there is no way back: the commit reaches it again.
    if (deal.state == State::Committed)
      break;
    auto reason = trade.reason == TradeReason::None || trade.reason == TradeReason::Over
                      ? TradeReason::Cancelled
                      : trade.reason;
    if (deal.state == State::Asked && side == 1 && reason == TradeReason::Cancelled)
      reason = TradeReason::Declined;
    out.push_back(Say(deal, other, TradeVerb::Close, reason));
    Finish(trade.id);
    break;
  }
  case TradeVerb::Confirm: {
    if (deal.state != State::Open || deal.confirmed[side])
      break;
    if (trade.mine != deal.offers[side].revision || trade.theirs != deal.offers[other].revision) {
      // It agreed to something else than what is on the table here (an
      // offer that crossed its confirmation, or was lost): it is told what
      // the table holds, and the other side's offer again if it missed it.
      if (trade.theirs != deal.offers[other].revision && deal.offers[other].revision != 0)
        out.push_back({from, deal.offers[other]});
      out.push_back({from, proto::Trade{.id = deal.id,
                                        .from = from,
                                        .to = from,
                                        .verb = TradeVerb::Withdraw,
                                        .mine = deal.offers[side].revision,
                                        .theirs = deal.offers[other].revision}});
      break;
    }
    deal.confirmed[side] = true;
    deal.sinceUs = nowUs;
    auto told = Say(deal, other, TradeVerb::Confirm, TradeReason::None);
    std::get<proto::Trade>(told.message).mine = trade.mine;
    std::get<proto::Trade>(told.message).theirs = trade.theirs;
    out.push_back(std::move(told));
    if (!deal.confirmed[other])
      break;
    // Both agree to the same table. A sale's price must be met, and there
    // must be something to trade; else both are asked again.
    if (deal.offers[0].asks > deal.offers[1].money || deal.offers[1].asks > deal.offers[0].money ||
        (Empty(deal.offers[0]) && Empty(deal.offers[1]))) {
      for (const int s : {0, 1}) {
        deal.confirmed[s] = false;
        out.push_back({deal.party[s], proto::Trade{.id = deal.id,
                                                   .from = deal.party[s],
                                                   .to = deal.party[s],
                                                   .verb = TradeVerb::Withdraw,
                                                   .reason = TradeReason::Invalid,
                                                   .mine = deal.offers[s].revision,
                                                   .theirs = deal.offers[1 - s].revision}});
      }
      break;
    }
    deal.state = State::Committed;
    deal.sinceUs = deal.resentUs = nowUs;
    ++m_committed;
    const proto::TradeCommit commit{
        .id = deal.id, .first = deal.offers[0], .second = deal.offers[1]};
    out.push_back({deal.party[0], commit});
    out.push_back({deal.party[1], commit});
    break;
  }
  case TradeVerb::Withdraw:
    if (deal.state == State::Open && deal.confirmed[side]) {
      deal.confirmed[side] = false;
      deal.sinceUs = nowUs;
      out.push_back(Say(deal, other, TradeVerb::Withdraw, TradeReason::None));
    }
    break;
  case TradeVerb::Applied:
    if (deal.state == State::Committed) {
      deal.applied[side] = true;
      if (deal.applied[other])
        Finish(trade.id);
    }
    break;
  }
  return out;
}

std::vector<Out> Table::On(proto::PlayerId from, const proto::TradeOffer &offer,
                           std::uint64_t nowUs) {
  std::vector<Out> out;
  const auto it = m_deals.find(offer.id);
  const int side = it == m_deals.end() ? -1 : Side(it->second, from);
  if (side < 0) {
    out.push_back(Over(offer.id, from));
    return out;
  }
  auto &deal = it->second;
  // Only at an open table; an older or repeated revision changes nothing.
  if (deal.state != State::Open || offer.revision <= deal.offers[side].revision)
    return out;
  if (!Valid(offer)) {
    out.push_back(Say(deal, 0, proto::TradeVerb::Close, proto::TradeReason::Invalid));
    out.push_back(Say(deal, 1, proto::TradeVerb::Close, proto::TradeReason::Invalid));
    Finish(offer.id);
    return out;
  }
  deal.offers[side] = offer;
  deal.offers[side].player = from;
  // The table changed: nobody has agreed to this one yet.
  deal.confirmed[0] = deal.confirmed[1] = false;
  deal.sinceUs = nowUs;
  out.push_back({deal.party[1 - side], deal.offers[side]});
  return out;
}

std::vector<Out> Table::Drop(proto::PlayerId player, proto::TradeReason reason) {
  std::vector<Out> out;
  std::vector<proto::TradeId> finished;
  for (auto &[id, deal] : m_deals) {
    const int side = Side(deal, player);
    if (side < 0)
      continue;
    if (deal.state == State::Committed) {
      // Decided: it stands. A player that is gone will never answer.
      if (reason == proto::TradeReason::Gone) {
        deal.applied[side] = true;
        if (deal.applied[1 - side])
          finished.push_back(id);
      }
      continue;
    }
    out.push_back(Say(deal, 1 - side, proto::TradeVerb::Close, reason));
    if (reason != proto::TradeReason::Gone)
      out.push_back(Say(deal, side, proto::TradeVerb::Close, reason));
    finished.push_back(id);
  }
  for (const auto id : finished)
    Finish(id);
  return out;
}

std::vector<Out> Table::Tick(std::uint64_t nowUs) {
  std::vector<Out> out;
  std::vector<proto::TradeId> finished;
  for (auto &[id, deal] : m_deals) {
    const auto age = nowUs - deal.sinceUs;
    if (deal.state == State::Committed) {
      if (age >= kCommitGiveUpUs) {
        finished.push_back(id); // told for a minute: its game is not listening
      } else if (nowUs - deal.resentUs >= kResendUs) {
        deal.resentUs = nowUs;
        const proto::TradeCommit commit{
            .id = deal.id, .first = deal.offers[0], .second = deal.offers[1]};
        for (const int side : {0, 1})
          if (!deal.applied[side])
            out.push_back({deal.party[side], commit});
      }
      continue;
    }
    const bool lapsed = deal.state == State::Asked && age >= kRequestLapseUs;
    const bool idle = deal.state == State::Open && age >= kIdleUs;
    if (!lapsed && !idle)
      continue;
    const auto reason = lapsed ? proto::TradeReason::Lapsed : proto::TradeReason::Idle;
    out.push_back(Say(deal, 0, proto::TradeVerb::Close, reason));
    out.push_back(Say(deal, 1, proto::TradeVerb::Close, reason));
    finished.push_back(id);
  }
  for (const auto id : finished)
    Finish(id);
  return out;
}
} // namespace tandem::trade

// ---- a game's seat ----

namespace tandem::trade {
void Seat::Leave() {
  m_state = State::None;
  m_id = 0;
  m_with = proto::kNoPlayer;
  m_mine = m_theirs = {};
  m_weConfirmed = m_theyConfirmed = false;
}

void Seat::SetYou(proto::PlayerId you) {
  if (you == m_you)
    return;
  m_you = you;
  Leave();
}

proto::Trade Seat::NotThere(proto::TradeId id, proto::PlayerId to) const {
  return {.id = id,
          .from = m_you,
          .to = to,
          .verb = proto::TradeVerb::Close,
          .reason = proto::TradeReason::Cancelled};
}

proto::Trade Seat::Say(proto::TradeVerb verb, proto::TradeReason reason) const {
  return {.id = m_id, .from = m_you, .to = m_with, .verb = verb, .reason = reason};
}

std::vector<proto::Message> Seat::Ask(proto::PlayerId to, std::uint64_t nowUs) {
  if (m_state != State::None || m_you == proto::kNoPlayer || to == m_you || to == proto::kNoPlayer)
    return {};
  m_state = State::Asking;
  m_id = proto::MakeTradeId(m_you, ++m_count);
  m_with = to;
  m_sinceUs = nowUs;
  return {Say(proto::TradeVerb::Request)};
}

std::vector<proto::Message> Seat::Answer(bool accept, std::uint64_t nowUs) {
  if (m_state != State::Asked)
    return {};
  if (!accept) {
    const auto no = Say(proto::TradeVerb::Close, proto::TradeReason::Declined);
    Leave();
    return {no};
  }
  m_state = State::Open;
  m_sinceUs = nowUs;
  m_mine = {.id = m_id, .player = m_you};
  m_theirs = {.id = m_id, .player = m_with};
  return {Say(proto::TradeVerb::Accept)};
}

std::vector<proto::Message> Seat::Offer(std::vector<proto::TradeItem> items, std::uint32_t money,
                                        std::uint32_t asks) {
  if (m_state != State::Open)
    return {};
  proto::TradeOffer offer{.id = m_id,
                          .player = m_you,
                          .revision = m_mine.revision + 1,
                          .money = money,
                          .asks = asks,
                          .items = std::move(items)};
  if (!Valid(offer))
    return {};
  m_mine = std::move(offer);
  // A changed table is one nobody agreed to.
  m_weConfirmed = m_theyConfirmed = false;
  return {m_mine};
}

std::vector<proto::Message> Seat::Confirm() {
  if (m_state != State::Open || m_weConfirmed)
    return {};
  m_weConfirmed = true;
  auto yes = Say(proto::TradeVerb::Confirm);
  yes.mine = m_mine.revision;
  yes.theirs = m_theirs.revision;
  return {yes};
}

std::vector<proto::Message> Seat::Withdraw() {
  if (m_state != State::Open || !m_weConfirmed)
    return {};
  m_weConfirmed = false;
  return {Say(proto::TradeVerb::Withdraw)};
}

std::vector<proto::Message> Seat::Close(proto::TradeReason reason) {
  if (m_state == State::None)
    return {};
  const auto bye = Say(proto::TradeVerb::Close, reason);
  Leave();
  return {bye};
}

Seat::Heard Seat::On(const proto::Message &message, std::uint64_t nowUs) {
  using Kind = Event::Kind;
  Heard heard;
  if (const auto *commit = std::get_if<proto::TradeCommit>(&message)) {
    // Decided, whatever we think of the table by now: it is applied.
    const bool first = commit->first.player == m_you;
    if (!first && commit->second.player != m_you)
      return heard; // not ours
    m_give = first ? commit->first : commit->second;
    m_take = first ? commit->second : commit->first;
    heard.events.push_back({Kind::Commit, m_take.player, proto::TradeReason::None});
    if (commit->id == m_id)
      Leave();
    return heard;
  }
  if (const auto *offer = std::get_if<proto::TradeOffer>(&message)) {
    if (offer->id != m_id || m_state == State::None) {
      heard.send.push_back(NotThere(offer->id, offer->player));
      return heard;
    }
    if (m_state != State::Open || offer->player != m_with || offer->revision <= m_theirs.revision)
      return heard;
    m_theirs = *offer;
    m_weConfirmed = m_theyConfirmed = false;
    heard.events.push_back({Kind::Offer, m_with, proto::TradeReason::None});
    return heard;
  }
  const auto *trade = std::get_if<proto::Trade>(&message);
  if (!trade)
    return heard;
  if (trade->verb == proto::TradeVerb::Request) {
    if (trade->id == m_id && m_state != State::None)
      return heard; // said again
    if (m_state != State::None) {
      // At another table: this one is refused, ours goes on.
      heard.send.push_back(proto::Trade{.id = trade->id,
                                        .from = m_you,
                                        .to = trade->from,
                                        .verb = proto::TradeVerb::Close,
                                        .reason = proto::TradeReason::Busy});
      return heard;
    }
    m_state = State::Asked;
    m_id = trade->id;
    m_with = trade->from;
    m_sinceUs = nowUs;
    heard.events.push_back({Kind::Asked, m_with, proto::TradeReason::None});
    return heard;
  }
  if (trade->id != m_id || m_state == State::None) {
    // About a trade we are not in (any more): if the other side still is
    // (our close was lost), it is told, through the host.
    if (trade->verb != proto::TradeVerb::Close && trade->from != m_you &&
        trade->from != proto::kNoPlayer)
      heard.send.push_back(NotThere(trade->id, trade->from));
    return heard;
  }
  switch (trade->verb) {
  case proto::TradeVerb::Accept:
    if (m_state == State::Asking) {
      m_state = State::Open;
      m_sinceUs = nowUs;
      m_mine = {.id = m_id, .player = m_you};
      m_theirs = {.id = m_id, .player = m_with};
      heard.events.push_back({Kind::Opened, m_with, proto::TradeReason::None});
    }
    break;
  case proto::TradeVerb::Close: {
    const auto with = m_with;
    Leave();
    heard.events.push_back({Kind::Closed, with, trade->reason});
    break;
  }
  case proto::TradeVerb::Confirm:
    // Their agreement counts for the table as we have it too.
    if (m_state == State::Open && trade->from == m_with && !m_theyConfirmed &&
        trade->mine == m_theirs.revision && trade->theirs == m_mine.revision) {
      m_theyConfirmed = true;
      heard.events.push_back({Kind::Confirmed, m_with, proto::TradeReason::None});
    }
    break;
  case proto::TradeVerb::Withdraw:
    if (m_state != State::Open)
      break;
    if (trade->from == m_you) {
      // The host's word on our own confirmation: it did not count. If it
      // has another offer of ours than we do, ours is sent again.
      m_weConfirmed = false;
      if (trade->reason == proto::TradeReason::Invalid)
        m_theyConfirmed = false;
      if (trade->mine != m_mine.revision) {
        ++m_mine.revision;
        heard.send.push_back(m_mine);
      }
      heard.events.push_back({Kind::Unconfirmed, m_with, trade->reason});
    } else if (m_theyConfirmed) {
      m_theyConfirmed = false;
      heard.events.push_back({Kind::Withdrawn, m_with, proto::TradeReason::None});
    }
    break;
  case proto::TradeVerb::Request:
  case proto::TradeVerb::Applied:
    break;
  }
  return heard;
}

Seat::Heard Seat::Tick(std::uint64_t nowUs) {
  Heard heard;
  // The host closes a request after kRequestLapseUs; this is for its word
  // getting lost.
  if ((m_state == State::Asking || m_state == State::Asked) &&
      nowUs - m_sinceUs >= kRequestLapseUs + 2'000'000) {
    heard.send.push_back(Say(proto::TradeVerb::Close, proto::TradeReason::Lapsed));
    const auto with = m_with;
    Leave();
    heard.events.push_back({Event::Kind::Closed, with, proto::TradeReason::Lapsed});
  }
  return heard;
}
} // namespace tandem::trade
