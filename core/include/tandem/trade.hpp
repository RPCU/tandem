#pragma once

// Trade between two players (docs/architecture/trade.md): the host's side.
//
// Inventories are each player's own (the authority table), so a trade is a
// promise both games keep: the host only arbitrates. One player asks, the
// other accepts; each sends its side of the table (TradeOffer, whole, with a
// revision) as it changes; each confirms the pair of revisions it sees; when
// both confirmations name the current pair the host commits (TradeCommit to
// both, with both offers) and each game gives what its offer says and takes
// what the other's says.
//
// What holds, whatever is lost, repeated or late:
//   - nothing changes hands before the commit: any close leaves both
//     inventories as they were;
//   - a commit is decided once, on two offers both parties confirmed
//     unchanged; after it nothing can call the trade off;
//   - the commit is sent again until its party answers Applied, and a game
//     applies a trade id once (Endpoint keeps the ids);
//   - a message about a trade the host no longer has is answered Close
//     (Over), a stale confirmation with the offer it missed, so both games
//     end up on the host's view.
// Platform-independent and without a clock or a socket of its own: the
// endpoint feeds it and sends what it returns; the tests drive it directly.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace tandem::trade {
inline constexpr std::uint64_t kRequestLapseUs = 15'000'000; // an unanswered request
inline constexpr std::uint64_t kIdleUs = 300'000'000;        // an open trade nobody touches
inline constexpr std::uint64_t kResendUs = 1'000'000;        // a commit not yet applied
inline constexpr std::uint64_t kCommitGiveUpUs = 60'000'000; // ...and never answered

// An offer the host passes on: within the limits, every stack with a
// quantity, none listed twice.
bool Valid(const proto::TradeOffer &offer);
// Nothing on the table: no item, no eddies.
bool Empty(const proto::TradeOffer &offer);

// A message for a player (the host's own included).
struct Out {
  proto::PlayerId to{};
  proto::Message message;
};

class Table {
public:
  // What a player says (`from`: who the connection says it is). `present`:
  // the players in the session now.
  std::vector<Out> On(proto::PlayerId from, const proto::Trade &trade, std::uint64_t nowUs,
                      const std::set<proto::PlayerId> &present);
  std::vector<Out> On(proto::PlayerId from, const proto::TradeOffer &offer, std::uint64_t nowUs);
  // A player can no longer trade: it left or vanished (Gone), went down
  // (Down), or its world was rebuilt around it (Away). Its trades close;
  // one already committed stands (a player that is gone has nothing more to
  // be told; one that is still there is told until it has applied it).
  std::vector<Out> Drop(proto::PlayerId player, proto::TradeReason reason);
  // Requests that lapsed, trades left open too long, commits to send again.
  std::vector<Out> Tick(std::uint64_t nowUs);

  // In a trade: asked, asking, at the table, or with a commit to apply.
  bool Busy(proto::PlayerId player) const;
  std::size_t Count() const { return m_deals.size(); }
  std::size_t Committed() const { return m_committed; }

private:
  enum class State : std::uint8_t { Asked, Open, Committed };
  struct Deal {
    proto::TradeId id{};
    proto::PlayerId party[2]{}; // [0] asked [1]
    State state{State::Asked};
    std::uint64_t sinceUs{}; // asked, last touched, or committed
    std::uint64_t resentUs{};
    proto::TradeOffer offers[2];
    bool confirmed[2]{};
    bool applied[2]{};
  };
  std::map<proto::TradeId, Deal> m_deals;
  // Trades that are over: a request that names one again is not a new one.
  std::deque<proto::TradeId> m_over;
  std::size_t m_committed{};

  static int Side(const Deal &deal, proto::PlayerId player);
  void Finish(proto::TradeId id);
  bool IsOver(proto::TradeId id) const;
  static Out Say(const Deal &deal, int toSide, proto::TradeVerb verb, proto::TradeReason reason);
  static Out Over(proto::TradeId id, proto::PlayerId to);
};
} // namespace tandem::trade

namespace tandem::trade {
// One game's seat at the table: what its player does becomes messages for
// the host, what the host says becomes events for the game. The plugin
// keeps one for the scripts, the mock player one for its options, and the
// tests one per player: all three follow the same rules, and a game that
// missed a message ends up on the host's view (a confirmation that did not
// count sends our offer again; a trade the host no longer has closes).
class Seat {
public:
  enum class State : std::uint8_t {
    None,   // not trading
    Asking, // we asked someone, no answer yet
    Asked,  // someone asked us
    Open,   // at the table
  };
  struct Event {
    enum class Kind : std::uint8_t {
      Asked,       // `with` asks us to trade
      Opened,      // the trade is open (they accepted, or we did)
      Offer,       // their side of the table changed
      Confirmed,   // they agree to the table as it is
      Withdrawn,   // they no longer do
      Unconfirmed, // our own agreement did not count (reason Invalid: the
                   // price is not met, or the table is empty)
      Closed,      // over, nothing changed hands (reason)
      Commit,      // decided: give `Give()`, take `Take()`, now
    };
    Kind kind{};
    proto::PlayerId with{};
    proto::TradeReason reason{};
  };
  struct Heard {
    std::vector<Event> events;
    std::vector<proto::Message> send;
  };

  // Our player id in the session (0: not in one; whatever was open is gone).
  void SetYou(proto::PlayerId you);

  // Our moves. Each returns what to send to the host (nothing: not now).
  std::vector<proto::Message> Ask(proto::PlayerId to, std::uint64_t nowUs);
  std::vector<proto::Message> Answer(bool accept, std::uint64_t nowUs);
  std::vector<proto::Message> Offer(std::vector<proto::TradeItem> items, std::uint32_t money,
                                    std::uint32_t asks);
  std::vector<proto::Message> Confirm();
  std::vector<proto::Message> Withdraw();
  std::vector<proto::Message> Close(proto::TradeReason reason);

  // What the host says (a Trade, a TradeOffer, or a TradeCommit the
  // endpoint let through: it comes once per trade).
  Heard On(const proto::Message &message, std::uint64_t nowUs);
  // A request nobody answered, should the host's word on it be lost.
  Heard Tick(std::uint64_t nowUs);

  State Is() const { return m_state; }
  proto::TradeId Id() const { return m_id; }
  proto::PlayerId With() const { return m_with; }
  const proto::TradeOffer &Mine() const { return m_mine; }
  const proto::TradeOffer &Theirs() const { return m_theirs; }
  bool WeConfirmed() const { return m_weConfirmed; }
  bool TheyConfirmed() const { return m_theyConfirmed; }
  // The last commit heard: what we give and what we take.
  const proto::TradeOffer &Give() const { return m_give; }
  const proto::TradeOffer &Take() const { return m_take; }

private:
  proto::PlayerId m_you{};
  State m_state{State::None};
  proto::TradeId m_id{};
  proto::PlayerId m_with{};
  std::uint32_t m_count{};
  std::uint64_t m_sinceUs{};
  proto::TradeOffer m_mine, m_theirs, m_give, m_take;
  bool m_weConfirmed{}, m_theyConfirmed{};

  void Leave();
  proto::Trade NotThere(proto::TradeId id, proto::PlayerId to) const;
  proto::Trade Say(proto::TradeVerb verb,
                   proto::TradeReason reason = proto::TradeReason::None) const;
};
} // namespace tandem::trade
