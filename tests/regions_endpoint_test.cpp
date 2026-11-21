// Per-player regions through real endpoints over loopback UDP
// (docs/architecture/regions.md): any game owns NPCs, linked players show
// them, requests reach the owner, ownership hands over when the owner
// leaves, under loss too, and an owner that vanishes leaves heirs.

#include "ports.hpp"
#include "referee_expect.hpp"
#include "slowdown.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <sstream>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using regions::MakeNpcId;
using test::NextPort;

constexpr Timing kQuick{
    .timeout = 1000ms, .takeover = 400ms, .digest = 100ms, .resyncCooldown = 200ms};

// What one game does with its endpoint's events: the proxies it shows, the
// NPCs it runs, and its answers to hand-overs (as Npcs.reds gives them).
struct Game {
  Endpoint *endpoint{};
  std::set<proto::NpcId> proxies, running;
  std::map<proto::NpcId, proto::NpcHandoverState> took;
  std::map<proto::NpcId, proto::Vec3> where;   // the NPCs we run
  std::map<proto::NpcId, proto::Vec3> shownAt; // where each proxy was spawned
  std::map<proto::NpcId, std::uint8_t> scales; // NpcScale heard (enemy scaling)
  int scaleWords{};
  std::vector<proto::NpcId> asked;
  int duplicateSpawns{}, strayGones{}, hits{}, strikes{}, effects{}, hacks{}, actions{}, replies{},
      noises{}, playerHits{}, cues{}, shotsAt{}, shotHits{}, states{}, lost{}, playerHacks{},
      areaHits{}, npcWords{};
  bool refuse{}, fail{};
  std::vector<proto::PlayerId> linked;
  proto::PlayerId leader{};
  std::map<proto::NpcId, float> levels; // the owners' NpcLevel
  std::vector<proto::Defended> defended;
  float giveHealth{0.4f};
  // The ledger (regions.md §14): the placed NPCs said, the deaths heard.
  std::map<proto::NpcId, std::uint64_t> placed;
  std::vector<proto::NpcDeath> deaths;

  void Announce(std::uint32_t counter, proto::Vec3 at = {}) {
    const auto npc = MakeNpcId(endpoint->You(), counter);
    running.insert(npc);
    endpoint->SendNpcSpawn({.npc = npc, .record = 100 + counter, .position = at});
    endpoint->SendNpcHealth({.npc = npc, .health = 1.f});
    Move(npc, at);
  }
  void Move(proto::NpcId npc, proto::Vec3 at) {
    where[npc] = at;
    endpoint->SendNpcStates({.npcs = {{.npc = npc, .position = at}}});
  }

  void See(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *spawn = std::get_if<events::HostNpcSpawn>(&event)) {
        duplicateSpawns += proxies.insert(spawn->spawn.npc).second ? 0 : 1;
        shownAt[spawn->spawn.npc] = spawn->spawn.position;
      } else if (const auto *gone = std::get_if<events::HostNpcGone>(&event))
        strayGones += proxies.erase(gone->gone.npc) > 0 ? 0 : 1;
      else if (const auto *s = std::get_if<events::HostNpcStates>(&event))
        states += static_cast<int>(s->states.npcs.size());
      else if (std::holds_alternative<events::NpcHitBy>(event))
        ++hits;
      else if (const auto *combat = std::get_if<events::Combat>(&event)) {
        strikes += std::holds_alternative<proto::NpcStrike>(combat->message) ? 1 : 0;
        areaHits += std::holds_alternative<proto::AreaHit>(combat->message) ? 1 : 0;
        if (const auto *level = std::get_if<proto::NpcLevel>(&combat->message))
          levels[level->npc] = level->level;
        if (const auto *defence = std::get_if<proto::Defended>(&combat->message))
          defended.push_back(*defence);
        if (const auto *place = std::get_if<proto::NpcPlaced>(&combat->message))
          placed[place->npc] = place->entity;
        if (const auto *death = std::get_if<proto::NpcDeath>(&combat->message))
          deaths.push_back(*death);
        npcWords += std::holds_alternative<proto::NpcWound>(combat->message) ||
                            std::holds_alternative<proto::NpcPoise>(combat->message) ||
                            std::holds_alternative<proto::NpcScale>(combat->message)
                        ? 1
                        : 0;
        if (const auto *scale = std::get_if<proto::NpcScale>(&combat->message)) {
          scales[scale->npc] = scale->players;
          ++scaleWords;
        }
      } else if (std::holds_alternative<events::HostPlayerHack>(event))
        ++playerHacks;
      // What an owner says about its NPC to the games that show it.
      else if (std::holds_alternative<events::HostNpcAnims>(event) ||
               std::holds_alternative<events::HostNpcHold>(event) ||
               std::holds_alternative<events::HostNpcShot>(event) ||
               std::holds_alternative<events::HostNpcHealth>(event) ||
               std::holds_alternative<events::HostNpcStatus>(event) ||
               std::holds_alternative<events::HostNpcHackState>(event) ||
               std::holds_alternative<events::HostActorPose>(event))
        ++npcWords;
      else if (std::holds_alternative<events::NpcEffectBy>(event))
        ++effects;
      else if (std::holds_alternative<events::NpcHackBy>(event))
        ++hacks;
      else if (const auto *action = std::get_if<events::NpcActionBy>(&event)) {
        ++actions;
        endpoint->SendNpcActionReply(
            action->action.player,
            {.npc = action->action.npc, .kind = action->action.kind, .accepted = true});
      } else if (std::holds_alternative<events::NpcActionAnswered>(event))
        ++replies;
      else if (std::holds_alternative<events::NoiseBy>(event))
        ++noises;
      else if (std::holds_alternative<events::HitByNpc>(event))
        ++playerHits;
      else if (std::holds_alternative<events::HostNpcCue>(event))
        ++cues;
      else if (std::holds_alternative<events::ShotAtUs>(event))
        ++shotsAt;
      else if (std::holds_alternative<events::ShotHitBy>(event))
        ++shotHits;
      else if (const auto *regions = std::get_if<events::RegionsChanged>(&event)) {
        linked = regions->linked;
        leader = regions->leader;
      } else if (const auto *give = std::get_if<events::NpcGive>(&event)) {
        asked.push_back(give->npc);
        if (refuse) {
          endpoint->RefuseNpc(give->npc);
          continue;
        }
        // Brain off, the body stays as a proxy.
        running.erase(give->npc);
        proxies.insert(give->npc);
        endpoint->GiveNpc({.npc = give->npc,
                           .to = give->to,
                           .position = where[give->npc],
                           .health = giveHealth,
                           .flags = proto::kHandoverCombat,
                           .target = give->to,
                           .effects = {{.record = 77, .remaining = 3.f}}});
      } else if (const auto *take = std::get_if<events::NpcTake>(&event)) {
        if (fail) {
          proxies.erase(take->state.npc);
          endpoint->AdoptNpc(take->state.npc, false);
          continue;
        }
        // The proxy wakes up.
        proxies.erase(take->state.npc);
        running.insert(take->state.npc);
        took[take->state.npc] = take->state;
        where[take->state.npc] = take->state.position;
        endpoint->AdoptNpc(take->state.npc, true);
      } else if (const auto *owner = std::get_if<events::NpcOwnerChanged>(&event)) {
        if (owner->was == endpoint->You() && running.erase(owner->npc) > 0) {
          // Taken from us without our giving it (our world was rebuilt).
          ++lost;
        }
      }
    }
  }
};

struct Session {
  std::unique_ptr<Endpoint> host, a, b;
  Game h, ga, gb;

  std::vector<Game *> Games() {
    std::vector<Game *> out{&h};
    if (a)
      out.push_back(&ga);
    if (b)
      out.push_back(&gb);
    return out;
  }

  bool Until(const std::function<bool()> &done, std::chrono::milliseconds limit = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + test::Patience(limit);
    while (std::chrono::steady_clock::now() < deadline) {
      for (auto *game : Games())
        game->See(game->endpoint->Update(1ms));
      if (done())
        return true;
    }
    return false;
  }
  void For(std::chrono::milliseconds time) {
    Until([] { return false; }, time);
  }
};

void Place(Endpoint &endpoint, float x) { endpoint.SendLocalState({.position = {x, 0.f, 0.f}}); }

std::unique_ptr<Session> Start(bool three = false, Timing timing = kQuick) {
  const auto port = NextPort();
  std::string error;
  auto s = std::make_unique<Session>();
  s->host = Endpoint::Host({"host"}, port, error, timing);
  s->a = Endpoint::Join({"a"}, "127.0.0.1", port, error, timing);
  REQUIRE((s->host && s->a));
  if (three) {
    s->b = Endpoint::Join({"b"}, "127.0.0.1", port, error, timing);
    REQUIRE(s->b);
  }
  s->h.endpoint = s->host.get();
  s->ga.endpoint = s->a.get();
  s->gb.endpoint = s->b.get();
  REQUIRE(s->Until([&] {
    return s->a->Players().size() == (three ? 3u : 2u) && (!three || s->b->Players().size() == 3u);
  }));
  return s;
}

std::set<proto::NpcId> Keys(const std::map<proto::NpcId, proto::NpcSpawn> &npcs) {
  std::set<proto::NpcId> out;
  for (const auto &[id, spawn] : npcs)
    out.insert(id);
  return out;
}
} // namespace

TEST_CASE("a guest runs NPCs of its own: the host and the other guest show them as proxies") {
  auto s = Start(true);
  s->ga.Announce(1);
  s->ga.Announce(2);
  const auto one = MakeNpcId(s->a->You(), 1), two = MakeNpcId(s->a->You(), 2);
  REQUIRE(s->Until([&] { return s->h.proxies.size() == 2 && s->gb.proxies.size() == 2; }));
  CHECK(s->host->NpcOwner(one) == s->a->You());
  CHECK(s->b->NpcOwner(two) == s->a->You());
  CHECK(s->a->OwnsNpc(one));
  CHECK_FALSE(s->host->OwnsNpc(one));
  CHECK(Keys(s->host->ProxiedNpcs()) == std::set<proto::NpcId>{one, two});
  CHECK(s->a->ProxiedNpcs().empty());
  // Its states reach both, never itself.
  REQUIRE(s->Until([&] { return s->h.states >= 2 && s->gb.states >= 2; }));
  CHECK(s->ga.states == 0);

  // The host's own NPC next to them: everyone's ids stay apart.
  s->h.Announce(1);
  REQUIRE(s->Until([&] { return s->ga.proxies.contains(1) && s->gb.proxies.size() == 3; }));
  CHECK(s->a->NpcOwner(1) == s->host->You());

  // Its death reaches both.
  s->a->SendNpcGone({.npc = one, .reason = proto::kGoneDead, .killer = s->b->You()});
  REQUIRE(s->Until([&] { return !s->h.proxies.contains(one) && !s->gb.proxies.contains(one); }));
  CHECK(s->host->NpcOwner(one) == proto::kNoPlayer);
  CHECK(s->h.duplicateSpawns + s->ga.duplicateSpawns + s->gb.duplicateSpawns == 0);
}

TEST_CASE("only the owner speaks for an NPC") {
  auto s = Start(true);
  s->h.Announce(5);
  s->ga.Announce(1);
  const auto theirs = MakeNpcId(s->a->You(), 1);
  REQUIRE(s->Until([&] { return s->gb.proxies.size() == 2; }));
  // b cannot announce in a's range, kill a's NPC, or move the host's.
  s->b->SendNpcSpawn({.npc = MakeNpcId(s->a->You(), 9), .record = 1});
  s->b->SendNpcGone({.npc = theirs, .reason = proto::kGoneDead});
  s->b->SendNpcHealth({.npc = 5, .health = 0.f});
  s->b->SendNpcGone({.npc = 5});
  s->For(300ms);
  CHECK(s->host->SharedNpcs().size() == 2);
  CHECK(s->h.proxies == std::set<proto::NpcId>{theirs});
  CHECK(s->ga.proxies == std::set<proto::NpcId>{5});
  CHECK(s->host->NpcOwner(theirs) == s->a->You());
}

TEST_CASE("hits, effects, hacks, actions and shots reach the NPC's owner, whoever it is") {
  auto s = Start(true);
  s->h.Announce(5);
  s->ga.Announce(1);
  const auto guests = MakeNpcId(s->a->You(), 1);
  REQUIRE(s->Until([&] { return s->gb.proxies.size() == 2 && s->h.proxies.size() == 1; }));

  // b on a's NPC: through the host, to a only.
  s->b->SendNpcHit({.npc = guests, .damage = 10.f});
  s->b->SendCombat(proto::NpcStrike{.npc = guests, .damage = 5.f});
  s->b->SendNpcEffect({.npc = guests, .effects = {1}});
  s->b->SendNpcHack({.npc = guests, .action = 3});
  s->b->SendNpcAction({.npc = guests, .kind = proto::NpcActionKind::Grab});
  REQUIRE(s->Until([&] {
    return s->ga.hits == 1 && s->ga.strikes == 1 && s->ga.effects == 1 && s->ga.hacks == 1 &&
           s->ga.actions == 1 && s->gb.replies == 1;
  }));
  CHECK(s->h.hits + s->h.strikes + s->h.effects + s->h.hacks + s->h.actions == 0);

  // The host on a's NPC: straight to a; a's answer comes back to the host.
  s->host->SendNpcHit({.npc = guests, .damage = 10.f});
  CHECK(s->host->SendCombat(proto::NpcStrike{.npc = guests, .damage = 5.f}));
  s->host->SendNpcAction({.npc = guests, .kind = proto::NpcActionKind::Takedown});
  REQUIRE(s->Until([&] { return s->ga.hits == 2 && s->ga.strikes == 2 && s->h.replies == 1; }));
  // The host's hit on its own NPC is its game's business.
  CHECK_FALSE(s->host->SendCombat(proto::NpcStrike{.npc = 5, .damage = 5.f}));

  // Guests on the host's NPC: as before.
  s->a->SendCombat(proto::NpcStrike{.npc = 5, .damage = 5.f});
  s->b->SendNpcAction({.npc = 5, .kind = proto::NpcActionKind::Grab});
  REQUIRE(s->Until([&] { return s->h.strikes == 1 && s->h.actions == 1 && s->gb.replies == 2; }));
  CHECK(s->ga.replies == 0);

  // a's NPC hits, cues and shoots at the host's V and b's: each to its game.
  CHECK(s->a->SendPlayerHit({.player = s->host->You(), .npc = guests, .physical = 5.f}));
  CHECK(s->a->SendPlayerHit({.player = s->b->You(), .npc = guests, .physical = 5.f}));
  s->a->SendNpcCue({.npc = guests, .player = s->b->You(), .cue = proto::NpcCueKind::CombatStart});
  CHECK(s->a->SendNpcShotAt({.player = s->host->You(), .npc = guests}));
  REQUIRE(s->Until([&] {
    return s->h.playerHits == 1 && s->gb.playerHits == 1 && s->gb.cues == 1 && s->h.shotsAt == 1;
  }));
  // ...and nobody speaks for an NPC that is not theirs.
  CHECK_FALSE(s->b->SendPlayerHit({.player = s->host->You(), .npc = guests}));
  CHECK_FALSE(s->b->SendNpcShotAt({.player = s->host->You(), .npc = guests}));
  // The shot the host's game simulated hit its V: a hears it.
  s->host->SendShotHit({.npc = guests});
  s->b->SendNoise({.stim = 1});
  REQUIRE(s->Until([&] { return s->ga.shotHits == 1 && s->ga.noises == 1 && s->h.noises == 1; }));
  CHECK(s->gb.noises == 0);
}

TEST_CASE("an NPC's level is its owner's word; a V's block of its attack goes to the owner") {
  auto s = Start(true);
  s->h.Announce(5);
  s->ga.Announce(1);
  const auto guests = MakeNpcId(s->a->You(), 1);
  const auto host = s->host->You(), a = s->a->You(), b = s->b->You();
  REQUIRE(s->Until([&] { return s->gb.proxies.size() == 2 && s->h.proxies.size() == 1; }));

  // a scales its own NPC (Levels.reds): the host and b give their proxies
  // the same, and the host keeps it for whoever comes next.
  CHECK(s->a->SendCombat(proto::NpcLevel{.npc = guests, .level = 12.5f}));
  CHECK(s->host->SendCombat(proto::NpcLevel{.npc = 5, .level = 30.f}));
  REQUIRE(s->Until([&] {
    return s->h.levels.contains(guests) && s->gb.levels.contains(guests) &&
           s->ga.levels.contains(5) && s->gb.levels.contains(5);
  }));
  CHECK(s->h.levels[guests] == 12.5f);
  CHECK(s->gb.levels[5] == 30.f);
  CHECK_FALSE(s->ga.levels.contains(guests)); // its own word, not echoed
  CHECK_FALSE(s->h.levels.contains(5));
  CHECK(s->host->NpcLevels().at(guests).level == 12.5f);
  // Nobody else speaks for it, nor for the host's.
  CHECK_FALSE(s->b->SendCombat(proto::NpcLevel{.npc = guests, .level = 1.f}));
  CHECK_FALSE(s->b->SendCombat(proto::NpcLevel{.npc = 5, .level = 1.f}));
  CHECK_FALSE(s->host->SendCombat(proto::NpcLevel{.npc = guests, .level = 1.f}));
  // The same level again changes nothing.
  CHECK(s->a->SendCombat(proto::NpcLevel{.npc = guests, .level = 12.5f}));
  s->For(200ms);
  CHECK(s->h.levels[guests] == 12.5f);
  CHECK(s->b->NpcLevels().at(guests).level == 12.5f);

  // Blocks and parries of a's NPC's attacks: to a, from the host and from b.
  CHECK(s->host->SendCombat(proto::Defended{.npc = guests, .how = proto::DefendKind::Blocked}));
  CHECK(s->b->SendCombat(proto::Defended{.npc = guests, .how = proto::DefendKind::Parried}));
  // Of the host's NPC: to the host, as before; a's own: its game's business.
  CHECK(s->a->SendCombat(proto::Defended{.npc = 5, .how = proto::DefendKind::Blocked}));
  CHECK_FALSE(s->a->SendCombat(proto::Defended{.npc = guests, .how = proto::DefendKind::Blocked}));
  CHECK_FALSE(s->host->SendCombat(proto::Defended{.npc = 5, .how = proto::DefendKind::Blocked}));
  REQUIRE(s->Until([&] { return s->ga.defended.size() == 2 && s->h.defended.size() == 1; }));
  s->For(200ms);
  CHECK(s->gb.defended.empty());
  REQUIRE(s->ga.defended.size() == 2);
  std::set<proto::PlayerId> by{s->ga.defended[0].player, s->ga.defended[1].player};
  CHECK(by == std::set<proto::PlayerId>{host, b});
  CHECK(s->h.defended[0].player == a);
  CHECK(s->h.defended[0].npc == 5);
}

TEST_CASE("an owner's word on its NPC reaches only the games that show it") {
  // The host and a linked; b just past the link distance from the host
  // (kLinkOut), farther still from a: it shows nobody's NPCs.
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 100);
  Place(*s->b, -(regions::kLinkOut + 5.f));
  const auto a = s->a->You(), b = s->b->You();
  s->h.Announce(1, {10.f, 0.f, 0.f});
  s->ga.Announce(1, {100.f, 0.f, 0.f});
  const auto theirs = MakeNpcId(a, 1);
  REQUIRE(s->Until([&] {
    return s->b->Apart() && !s->host->LinkedWith(b) && s->ga.proxies.contains(1) &&
           s->h.proxies.contains(theirs);
  }));
  REQUIRE(s->Until([&] { return s->gb.proxies.empty(); }));
  s->host->SendCombat(proto::Rules{.flags = proto::kRuleFriendlyFire});
  s->For(100ms);
  const auto quiet = s->gb.npcWords;

  // Everything each owner says about its NPC, to b and to everyone.
  const auto speak = [&](Endpoint &owner, proto::NpcId npc, proto::PlayerId to) {
    owner.SendPlayerHit({.player = to, .npc = npc, .physical = 5.f});
    owner.SendNpcCue({.npc = npc, .player = to, .cue = proto::NpcCueKind::CombatStart});
    owner.SendNpcShotAt({.player = to, .npc = npc});
    owner.SendPlayerHack({.player = to, .npc = npc, .action = 1});
    owner.SendNpcShot({.npc = npc});
    owner.SendNpcAnims({.npcs = {{.npc = npc, .stance = 1}}});
    owner.SendNpcHold({.npc = npc, .player = to});
    owner.SendNpcHold({.npc = npc});
    owner.SendCombat(proto::NpcWound{.npc = npc, .part = 1, .wound = 1});
    owner.SendCombat(proto::NpcPoise{.npc = npc, .poise = 0.5f});
    owner.SendNpcHealth({.npc = npc, .health = 0.9f});
    owner.SendCombat(proto::NpcScale{.npc = npc, .players = 2});
  };
  speak(*s->host, 1, b);
  speak(*s->a, theirs, b);
  // The same aimed at the games that do show them: these arrive.
  speak(*s->host, 1, a);
  speak(*s->a, theirs, 1);
  REQUIRE(s->Until([&] {
    return s->ga.playerHits == 1 && s->ga.cues == 1 && s->ga.shotsAt == 1 &&
           s->ga.playerHacks == 1 && s->h.playerHits == 1 && s->h.cues == 1 && s->h.shotsAt == 1 &&
           s->h.playerHacks == 1;
  }));
  // A V's own hit on another V is not an NPC's: it reaches its target's game.
  CHECK(s->a->SendCombat(proto::AreaHit{.player = b, .physical = 5.f}));
  REQUIRE(s->Until([&] { return s->gb.areaHits == 1; }));
  s->For(200ms);
  CHECK(s->gb.playerHits == 0);
  CHECK(s->gb.cues == 0);
  CHECK(s->gb.shotsAt == 0);
  CHECK(s->gb.playerHacks == 0);
  CHECK(s->gb.npcWords == quiet);
  CHECK(s->gb.scales.empty());
  // The scale reached the games that show the NPC.
  CHECK(s->ga.scales[1] == 2);
  CHECK(s->h.scales[theirs] == 2);
  CHECK(s->gb.proxies.empty());
  CHECK(s->gb.duplicateSpawns == 0);
}

TEST_CASE("players apart share no NPC; meeting shows both sets; parting drops them again") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 1000);
  REQUIRE(s->Until([&] { return !s->a->LinkedWith(1) && !s->host->LinkedWith(s->a->You()); }));
  CHECK(s->ga.linked.empty());

  s->h.Announce(1);
  s->ga.Announce(1, {1000.f, 0.f, 0.f});
  const auto theirs = MakeNpcId(s->a->You(), 1);
  s->For(400ms);
  CHECK(s->h.proxies.empty());
  CHECK(s->ga.proxies.empty());
  CHECK(s->a->ProxiedNpcs().empty());
  CHECK(s->host->ProxiedNpcs().empty());
  // The record is whole all the same, and each runs its own.
  CHECK(s->host->SharedNpcs().size() == 2);
  CHECK(s->a->OwnsNpc(theirs));
  // Nothing was asked again for it: the digests agree on "nothing to show".
  CHECK(s->a->Checks().Requests() == 0);

  // The guest comes over.
  Place(*s->a, 100);
  REQUIRE(s->Until([&] { return s->h.proxies.contains(theirs) && s->ga.proxies.contains(1); }));
  CHECK(s->ga.linked == std::vector<proto::PlayerId>{1});
  CHECK(s->h.linked == std::vector<proto::PlayerId>{s->a->You()});
  // Sticky: nobody's NPC moved by meeting.
  CHECK(s->a->OwnsNpc(theirs));
  CHECK(s->host->OwnsNpc(1));

  // ...and leaves again.
  Place(*s->a, 1000);
  REQUIRE(s->Until([&] { return s->h.proxies.empty() && s->ga.proxies.empty(); }));
  CHECK(s->a->OwnsNpc(theirs));
  CHECK(s->h.strayGones + s->ga.strayGones == 0);
  CHECK(s->h.duplicateSpawns + s->ga.duplicateSpawns == 0);
  s->For(400ms);
  CHECK(s->a->Checks().Requests() == 0);
}

TEST_CASE("a link shows an owner's NPCs where they are, not where it announced them") {
  // regions-meet: a guest announced its NPCs 400 m out and walked up with
  // them. Linked with nobody, it sent no states, so the link showed them
  // from the spawn's place: the host's game ignored a spawn beyond 250 m and
  // its consistency check made the proxies 2.5 s later.
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->b, 0);
  Place(*s->a, 1000);
  const auto a = s->a->You();
  REQUIRE(s->Until([&] { return s->a->Apart() && !s->host->LinkedWith(a); }));
  s->ga.Announce(1, {1000.f, 0.f, 0.f});
  const auto theirs = MakeNpcId(a, 1);
  REQUIRE(s->Until([&] { return s->host->SharedNpcs().contains(theirs); }));

  // It walks to 400 m with its NPC, still apart, its game saying where the
  // NPC is ten times a second: the host's record follows, nobody shows it.
  Place(*s->a, 400);
  REQUIRE(s->Until(
      [&] {
        s->ga.Move(theirs, {405.f, 0.f, 0.f});
        return s->host->SharedNpcs().at(theirs).position.x == doctest::Approx(405.f);
      },
      3s));
  CHECK(s->a->Apart());
  CHECK(s->h.proxies.empty());
  CHECK(s->gb.proxies.empty());
  CHECK(s->h.states == 0);
  CHECK(s->gb.states == 0);

  // Linked: the host's game and b show it where it was last said to be.
  Place(*s->a, 100);
  REQUIRE(
      s->Until([&] { return s->h.proxies.contains(theirs) && s->gb.proxies.contains(theirs); }));
  CHECK(s->h.shownAt[theirs].x == doctest::Approx(405.f));
  CHECK(s->gb.shownAt[theirs].x == doctest::Approx(405.f));
  CHECK(s->h.duplicateSpawns + s->gb.duplicateSpawns == 0);
}

TEST_CASE("a lost Regions is healed by the players digest: the owner's states flow again") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 1000);
  s->ga.Announce(1, {1000.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->a->Apart() && !s->host->LinkedWith(s->a->You()); }));
  // The guest comes over while everything the host says is lost: the link
  // forms on the host, and its Regions never arrives. Links do not change
  // again: only the digest can tell the guest.
  s->host->SetFaults({.drop = 1.0});
  Place(*s->a, 100);
  REQUIRE(s->Until([&] { return s->host->LinkedWith(s->a->You()); }));
  s->For(50ms);
  s->host->SetFaults({});
  CHECK(s->a->Apart());
  REQUIRE(s->Until([&] { return s->a->LinkedWith(1); }));
  CHECK(s->a->Checks().Requests() >= 1);
  // Linked in its own view, the owner sends its NPC's states again.
  const auto theirs = MakeNpcId(s->a->You(), 1);
  const auto before = s->h.states;
  s->ga.Move(theirs, {120.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->h.states > before; }));
  CHECK(s->host->SharedNpcs().at(theirs).position.x == doctest::Approx(120.f));
}

TEST_CASE("every game knows who runs the area around each player (traffic's AreaOwner)") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 1000);
  const auto a = s->a->You();
  REQUIRE(
      s->Until([&] { return s->a->Apart() && s->a->OwnsArea() && s->host->AreaOwner(a) == a; }));
  CHECK(s->host->OwnsArea());
  CHECK(s->a->AreaOwner(1) == 1u);

  // Deep in the host's region: the host's game runs the world there, in
  // every game's view.
  Place(*s->a, 30);
  REQUIRE(s->Until([&] { return !s->a->OwnsArea() && s->host->AreaOwner(a) == 1u; }));
  CHECK(s->a->AreaOwner(a) == 1u);
  CHECK(s->host->OwnsArea());
  // A step back out, still within the radius: no change (hysteresis).
  Place(*s->a, 55);
  s->For(600ms);
  CHECK_FALSE(s->a->OwnsArea());
  // Out of it: its own again.
  Place(*s->a, 70);
  REQUIRE(s->Until([&] { return s->a->OwnsArea() && s->host->AreaOwner(a) == a; }));
}

TEST_CASE("three players in a row: links are pairwise") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 120);
  Place(*s->b, 240);
  s->h.Announce(1);
  s->ga.Announce(1, {120.f, 0.f, 0.f});
  s->gb.Announce(1, {240.f, 0.f, 0.f});
  const auto as = MakeNpcId(s->a->You(), 1), bs = MakeNpcId(s->b->You(), 1);
  REQUIRE(s->Until([&] {
    return s->h.proxies == std::set<proto::NpcId>{as} &&
           s->ga.proxies == std::set<proto::NpcId>{1, bs} &&
           s->gb.proxies == std::set<proto::NpcId>{as};
  }));
}

TEST_CASE("an NPC is handed over when its owner walks away from it: host to guest and back") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 40);
  s->h.Announce(1, {30.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->ga.proxies.contains(1); }));
  s->For(400ms);
  CHECK(s->h.asked.empty()); // both near: nothing moves

  // The host walks off; the guest stays 10 m from the NPC.
  Place(*s->host, -60);
  REQUIRE(s->Until([&] { return s->ga.running.contains(1) && s->host->NpcOwner(1) == 2u; }));
  CHECK(s->a->OwnsNpc(1));
  CHECK_FALSE(s->host->OwnsNpc(1));
  // No pop: the giver keeps its body as a proxy, the receiver's proxy woke up.
  CHECK(s->h.proxies == std::set<proto::NpcId>{1});
  CHECK(s->ga.proxies.empty());
  CHECK(s->h.running.empty());
  // The state came with it.
  const auto &state = s->ga.took.at(1);
  CHECK(state.health == doctest::Approx(0.4f));
  CHECK(state.target == s->a->You());
  CHECK(state.from == s->host->You());
  CHECK((state.flags & proto::kHandoverCombat) != 0);
  REQUIRE(state.effects.size() == 1);
  CHECK(state.effects[0].record == 77);
  REQUIRE(s->Until([&] { return s->host->Handovers() == 1; }));

  // The new owner's states and death are now the word on it.
  s->ga.Move(1, {31.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->h.states > 0; }));
  // The old owner's are not.
  s->host->SendNpcHealth({.npc = 1, .health = 0.f});
  s->host->SendNpcGone({.npc = 1, .reason = proto::kGoneDead});
  s->For(200ms);
  CHECK(s->host->SharedNpcs().contains(1));

  // And back: the guest walks off, the host returns.
  Place(*s->host, 20);
  s->For(300ms);
  Place(*s->a, 140);
  REQUIRE(s->Until([&] { return s->h.running.contains(1) && s->a->NpcOwner(1) == 1u; }));
  CHECK(s->host->OwnsNpc(1));
  CHECK(s->ga.proxies == std::set<proto::NpcId>{1});
  CHECK(s->h.duplicateSpawns + s->ga.duplicateSpawns == 0);
  CHECK(s->h.strayGones + s->ga.strayGones == 0);
}

TEST_CASE("a hand-over between two guests, seen by the host") {
  auto s = Start(true);
  Place(*s->host, 20);
  Place(*s->a, 0);
  Place(*s->b, 40);
  s->ga.Announce(1, {35.f, 0.f, 0.f});
  const auto npc = MakeNpcId(s->a->You(), 1);
  REQUIRE(s->Until([&] { return s->gb.proxies.contains(npc) && s->h.proxies.contains(npc); }));
  // a leaves; the host (20 m... 15 m from it) outranks b: the host takes it.
  Place(*s->a, -70);
  REQUIRE(s->Until([&] { return s->h.running.contains(npc); }));
  CHECK(s->host->OwnsNpc(npc));
  REQUIRE(s->Until([&] { return s->b->NpcOwner(npc) == 1u && s->a->NpcOwner(npc) == 1u; }));
  CHECK(s->gb.proxies.contains(npc));
  // The host leaves too: b is the one left near it.
  Place(*s->host, -60);
  REQUIRE(s->Until([&] { return s->gb.running.contains(npc); }));
  REQUIRE(s->Until([&] { return s->a->NpcOwner(npc) == s->b->You(); }));
  CHECK(s->h.duplicateSpawns + s->ga.duplicateSpawns + s->gb.duplicateSpawns == 0);
}

TEST_CASE("an owner may refuse a hand-over; pinned NPCs are never asked for") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 40);
  s->h.refuse = true;
  s->h.Announce(1, {30.f, 0.f, 0.f});
  s->h.Announce(2, {30.f, 0.f, 0.f});
  s->host->PinNpc(2);
  REQUIRE(s->Until([&] { return s->ga.proxies.size() == 2; }));
  Place(*s->host, -60);
  REQUIRE(s->Until([&] { return !s->h.asked.empty(); }));
  s->For(600ms);
  // Asked once for the first (then left alone for a while), never for the pinned.
  CHECK(s->h.asked == std::vector<proto::NpcId>{1});
  CHECK(s->host->OwnsNpc(1));
  CHECK(s->host->OwnsNpc(2));
  CHECK(s->ga.running.empty());

  // A guest's pin reaches the host's arbitration too.
  s->ga.Announce(1, {35.f, 0.f, 0.f});
  const auto theirs = MakeNpcId(s->a->You(), 1);
  s->a->PinNpc(theirs);
  Place(*s->host, 30);
  REQUIRE(s->Until([&] { return s->h.proxies.contains(theirs); }));
  Place(*s->a, 130);
  s->For(800ms);
  CHECK(s->ga.asked.empty());
  CHECK(s->a->OwnsNpc(theirs));
}

TEST_CASE("a receiver that cannot run the NPC: it goes for everyone, never left ownerless") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 40);
  s->ga.fail = true;
  s->h.Announce(1, {30.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->ga.proxies.contains(1); }));
  Place(*s->host, -60);
  REQUIRE(s->Until([&] { return s->host->NpcOwner(1) == proto::kNoPlayer; }));
  REQUIRE(s->Until([&] { return s->a->NpcOwner(1) == proto::kNoPlayer; }));
  CHECK(s->host->SharedNpcs().empty());
  CHECK(s->a->SharedNpcs().empty());
}

TEST_CASE("hand-overs go through although a third of everything is lost") {
  // Lost and doubled on purpose: states twice, resyncs that a lossy link undoes.
  test::ExpectReferee({"oneshot.seq-repeated", "digest.resync-loop"});
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 40);
  for (std::uint32_t i = 1; i <= 12; ++i)
    s->h.Announce(i, {30.f, 0.f, 0.f});
  for (std::uint32_t i = 1; i <= 12; ++i)
    s->ga.Announce(i, {35.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->ga.proxies.size() == 12 && s->h.proxies.size() == 12; }));

  s->host->SetFaults({.drop = 0.33, .dup = 0.33, .seed = 5});
  s->a->SetFaults({.drop = 0.33, .dup = 0.33, .seed = 9});
  // The host leaves its twelve with the guest (its place, sent again and
  // again as a game does: a state can be lost).
  const auto walk = [&](float hostX, float guestX, std::chrono::milliseconds time) {
    const auto until = std::chrono::steady_clock::now() + time;
    while (std::chrono::steady_clock::now() < until) {
      Place(*s->host, hostX);
      Place(*s->a, guestX);
      s->For(30ms);
    }
  };
  walk(-60, 40, 4s);
  s->host->SetFaults({});
  s->a->SetFaults({});
  walk(-60, 40, 300ms);

  // Whatever was lost on the way, both agree on who runs each NPC, every
  // NPC has exactly one game running it, and nobody shows what is not there.
  // On a failure: who each game says runs each NPC, and what each runs.
  const auto describe = [&] {
    std::ostringstream out;
    auto all = Keys(s->host->SharedNpcs());
    for (const auto &[npc, spawn] : s->a->SharedNpcs())
      all.insert(npc);
    for (const auto npc : all)
      out << "\nnpc " << npc << ": owner " << s->host->NpcOwner(npc) << " (host), "
          << s->a->NpcOwner(npc) << " (guest); runs: host " << s->h.running.contains(npc)
          << ", guest " << s->ga.running.contains(npc) << "; in the record of: host "
          << s->host->SharedNpcs().contains(npc) << ", guest " << s->a->SharedNpcs().contains(npc);
    return out.str();
  };
  INFO(describe());
  REQUIRE(s->Until(
      [&] {
        for (const auto &[npc, spawn] : s->host->SharedNpcs()) {
          const auto owner = s->host->NpcOwner(npc);
          if (s->a->NpcOwner(npc) != owner)
            return false;
          const bool hostRuns = s->h.running.contains(npc), guestRuns = s->ga.running.contains(npc);
          if (hostRuns == guestRuns || hostRuns != (owner == 1u))
            return false;
        }
        return Keys(s->a->SharedNpcs()) == Keys(s->host->SharedNpcs());
      },
      6s));
  // Most were handed over (a hand-over given up after its retries leaves
  // the NPC with its owner, or lets it go: never with two or none).
  std::size_t moved = 0;
  for (std::uint32_t i = 1; i <= 12; ++i)
    moved += s->a->OwnsNpc(i) ? 1 : 0;
  CHECK(moved >= 6);
  CHECK(s->ga.took.size() >= moved);
  // The guest's own never moved: its V stayed with them.
  for (std::uint32_t i = 1; i <= 12; ++i)
    if (s->host->SharedNpcs().contains(MakeNpcId(2, i)))
      CHECK(s->host->NpcOwner(MakeNpcId(2, i)) == 2u);
}

TEST_CASE("an owner that vanishes: the players near its NPCs inherit them, the rest go") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 30);
  Place(*s->b, 100);
  const auto owner = s->a->You();
  s->ga.Announce(1, {10.f, 0.f, 0.f});   // next to the host
  s->ga.Announce(2, {90.f, 0.f, 0.f});   // next to b only
  s->ga.Announce(3, {30.f, 300.f, 0.f}); // near nobody
  s->ga.Announce(4, {10.f, 0.f, 0.f});   // a quest NPC of its own: pinned
  s->a->SendNpcHealth({.npc = MakeNpcId(owner, 1), .health = 0.6f});
  s->a->PinNpc(MakeNpcId(owner, 4));
  REQUIRE(s->Until([&] { return s->h.proxies.size() == 4 && s->gb.proxies.size() == 4; }));
  s->For(150ms);

  s->a.reset(); // no goodbye
  REQUIRE(
      s->Until([&] { return s->host->Players().size() == 2 && s->b->Players().size() == 2; }, 4s));
  const auto one = MakeNpcId(owner, 1), two = MakeNpcId(owner, 2);
  REQUIRE(s->Until([&] { return s->h.running.contains(one) && s->gb.running.contains(two); }));
  CHECK(s->host->Inherited() == 2);
  // The heir got what the session knew of it.
  const auto &state = s->h.took.at(one);
  CHECK((state.flags & proto::kHandoverInherited) != 0);
  CHECK((state.flags & proto::kHandoverBlind) != 0);
  CHECK(state.health == doctest::Approx(0.6f));
  CHECK(state.target == proto::kNoPlayer);
  // Everyone agrees, and the rest is gone for everyone.
  REQUIRE(s->Until([&] {
    return s->b->NpcOwner(one) == 1u && s->host->NpcOwner(two) == s->b->You() &&
           s->host->SharedNpcs().size() == 2 && s->b->SharedNpcs().size() == 2;
  }));
  CHECK(s->h.proxies == std::set<proto::NpcId>{two});
  CHECK(s->gb.proxies == std::set<proto::NpcId>{one});
  CHECK(s->h.duplicateSpawns + s->gb.duplicateSpawns == 0);
  CHECK(s->h.strayGones + s->gb.strayGones == 0);
}

TEST_CASE("an owner whose world is rebuilt leaves its NPCs to who stays; the host's too") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 30);
  s->ga.Announce(1, {10.f, 0.f, 0.f});
  s->ga.Announce(2, {30.f, 400.f, 0.f});
  const auto one = MakeNpcId(s->a->You(), 1), two = MakeNpcId(s->a->You(), 2);
  REQUIRE(s->Until([&] { return s->h.proxies.size() == 2; }));

  // The guest fast travels: the host, next to the first, takes it over.
  s->a->BumpEpoch(proto::kEpochFastTravel);
  REQUIRE(s->Until([&] { return s->h.running.contains(one) && s->ga.lost == 1; }));
  CHECK(s->host->OwnsNpc(one));
  CHECK(s->a->NpcOwner(one) == 1u);
  // The one nobody is near stays its own: its game says what became of it.
  CHECK(s->a->OwnsNpc(two));
  s->a->SendNpcGone({.npc = two});
  REQUIRE(s->Until([&] { return s->h.proxies.empty(); }));
  CHECK(s->ga.running == std::set<proto::NpcId>{two});
  s->ga.running.clear();

  // The host fast travels with one NPC of its own next to the guest.
  s->h.Announce(7, {28.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->ga.proxies.contains(7); }));
  s->host->BumpEpoch(proto::kEpochFastTravel);
  REQUIRE(s->Until([&] { return s->ga.running.contains(7) && s->ga.running.contains(one); }));
  CHECK(s->a->OwnsNpc(7));
  CHECK(s->h.lost == 2);
  CHECK(s->h.running.empty());
}

TEST_CASE("an old owner that missed the word on its NPC's new owner stops running it") {
  // The old owner speaks for an NPC it no longer runs, on purpose: the host drops it.
  test::ExpectReferee({"authority.not-owner"});
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 30);
  s->ga.Announce(1, {10.f, 0.f, 0.f});
  const auto npc = MakeNpcId(s->a->You(), 1);
  REQUIRE(s->Until([&] { return s->h.proxies.contains(npc); }));
  // The guest's world is rebuilt: the host, next to the NPC, inherits it,
  // and its word to the guest is lost.
  s->host->SetFaults({.drop = 1.0});
  s->a->BumpEpoch(proto::kEpochFastTravel);
  REQUIRE(s->Until([&] { return s->h.running.contains(npc); }));
  s->For(50ms);
  s->host->SetFaults({});
  CHECK(s->ga.running.contains(npc)); // two brains for now
  REQUIRE(s->Until([&] { return !s->ga.running.contains(npc); }));
  CHECK(s->a->NpcOwner(npc) == 1u);
  CHECK(s->ga.lost == 1);
  CHECK(s->host->OwnsNpc(npc));
}

TEST_CASE("the host's record of a guest's NPCs is healed when a spawn or a removal was lost") {
  auto s = Start();
  // Everything the guest says is lost for a moment: two spawns never arrive.
  s->a->SetFaults({.drop = 1.0});
  s->ga.Announce(1);
  s->ga.Announce(2);
  s->For(100ms);
  s->a->SetFaults({});
  s->ga.Announce(3);
  const auto id = [&](std::uint32_t i) { return MakeNpcId(s->a->You(), i); };
  REQUIRE(s->Until([&] { return s->h.proxies == std::set<proto::NpcId>{id(1), id(2), id(3)}; }));
  // A removal lost the same way.
  s->a->SetFaults({.drop = 1.0});
  s->a->SendNpcGone({.npc = id(2)});
  s->For(100ms);
  s->a->SetFaults({});
  REQUIRE(s->Until([&] { return s->h.proxies == std::set<proto::NpcId>{id(1), id(3)}; }));
  CHECK(s->host->SharedNpcs().size() == 2);
  CHECK(s->h.duplicateSpawns == 0);
}

TEST_CASE("a late joiner sees who owns what, and only what is near it") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 40);
  s->h.Announce(1, {30.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->ga.proxies.contains(1); }));
  Place(*s->host, -60);
  REQUIRE(s->Until([&] { return s->a->OwnsNpc(1); }));

  std::string error;
  s->b = Endpoint::Join({"b"}, "127.0.0.1", s->host->LocalPort(), error, kQuick);
  REQUIRE(s->b);
  s->gb.endpoint = s->b.get();
  REQUIRE(s->Until([&] { return s->gb.proxies.contains(1); }));
  // Handed over before it came: it learns the owner with the NPC.
  CHECK(s->b->NpcOwner(1) == s->a->You());
  // Far from everyone once its place is known: nothing left to show.
  Place(*s->b, 5000);
  REQUIRE(s->Until([&] { return s->gb.proxies.empty() && s->gb.linked.empty(); }));
  s->For(400ms);
  CHECK(s->b->Checks().Requests() == 0);
}

TEST_CASE("grouped or apart: one fact per player, and a region held for a gig") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 1000);
  Place(*s->b, 20);
  REQUIRE(s->Until([&] { return s->a->Apart() && !s->b->Apart() && !s->host->Apart(); }));
  // Apart, a player leads itself; grouped, the host leads.
  CHECK(s->a->RegionLeader() == s->a->You());
  CHECK(s->b->RegionLeader() == 1u);
  CHECK(s->host->RegionLeader() == 1u);
  REQUIRE(s->Until([&] { return s->ga.leader == s->a->You() && s->gb.leader == 1u; }));

  // The far guest starts a gig of its own world and holds its region; it
  // has NPCs of that gig around it.
  s->a->SetRegionHold(true);
  s->ga.Announce(1, {1000.f, 0.f, 0.f});
  const auto npc = MakeNpcId(s->a->You(), 1);
  REQUIRE(s->Until([&] { return s->host->RegionHolders().contains(s->a->You()); }));
  REQUIRE(s->Until([&] { return s->b->RegionHolders().contains(s->a->You()); }));

  // The host arrives: it is the helper. The holder leads the group, the
  // host's world waits; the holder's NPCs are shown, as proxies.
  Place(*s->host, 990);
  Place(*s->b, 5000);
  REQUIRE(s->Until([&] {
    return s->h.leader == s->a->You() && s->ga.leader == s->a->You() && s->h.proxies.contains(npc);
  }));
  CHECK(s->host->RegionLeader() == s->a->You());

  // The holder walks to the next room, 90 m on; the host stays by the NPC:
  // no hand-over while the gig runs.
  Place(*s->a, 1100);
  s->ga.Move(npc, {1000.f, 0.f, 0.f});
  s->For(800ms);
  CHECK(s->ga.asked.empty());
  CHECK(s->a->OwnsNpc(npc));

  // The gig is over: the host leads again, and the NPC left behind is its.
  s->a->SetRegionHold(false);
  REQUIRE(s->Until([&] { return s->h.leader == 1u && s->ga.leader == 1u; }));
  REQUIRE(s->Until([&] { return s->host->OwnsNpc(npc); }));

  // A holder that vanishes holds nothing.
  s->a->SetRegionHold(true);
  REQUIRE(s->Until([&] { return s->b->RegionHolders().contains(s->a->You()); }));
  s->a.reset();
  REQUIRE(s->Until(
      [&] { return s->b->RegionHolders().empty() && s->host->RegionHolders().empty(); }, 4s));
}

TEST_CASE("an NPC's scale: its owner's word, to everyone who sees it, kept, and the heir's next") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 40);
  Place(*s->b, 20);
  // Said before the spawn reached the endpoint (another queue in the game):
  // kept, and said right after it.
  const auto theirs = MakeNpcId(s->a->You(), 1);
  CHECK_FALSE(s->a->SendCombat(proto::NpcScale{.npc = theirs, .players = 2}));
  s->ga.Announce(1, {35.f, 0.f, 0.f});
  s->h.Announce(1, {30.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->h.scales.contains(theirs) && s->gb.scales.contains(theirs); }));
  CHECK(s->h.scales[theirs] == 2);
  CHECK(s->ga.scales.empty()); // never its own back

  // The host's own: to both guests; a repeat is said once.
  CHECK(s->host->SendCombat(proto::NpcScale{.npc = 1, .players = 3}));
  CHECK(s->host->SendCombat(proto::NpcScale{.npc = 1, .players = 3}));
  REQUIRE(s->Until([&] { return s->ga.scales[1] == 3 && s->gb.scales[1] == 3; }));
  s->For(200ms);
  CHECK(s->ga.scaleWords == 1);
  // Only the owner speaks for it.
  CHECK_FALSE(s->b->SendCombat(proto::NpcScale{.npc = 1, .players = 1}));
  CHECK_FALSE(s->host->SendCombat(proto::NpcScale{.npc = theirs, .players = 4}));
  s->For(200ms);
  CHECK(s->ga.scales[1] == 3);
  CHECK(s->h.scales[theirs] == 2);
  // Every game keeps what it knows, the same: the digests agree.
  CHECK(s->host->NpcScales().at(1).players == 3);
  CHECK(s->b->NpcScales().at(1).players == 3);
  CHECK(s->b->NpcScales().at(theirs).players == 2);

  // A late joiner hears the scales with the NPCs.
  s->a->Leave();
  s->a.reset();
  s->ga.endpoint = nullptr;
  REQUIRE(s->Until([&] { return s->host->Players().size() == 2; }));
  std::string error;
  s->a = Endpoint::Join({"late"}, "127.0.0.1", s->host->LocalPort(), error, kQuick);
  REQUIRE(s->a);
  s->ga = Game{};
  s->ga.endpoint = s->a.get();
  Place(*s->a, 25);
  REQUIRE(s->Until([&] { return s->ga.scales.contains(1); }));
  CHECK(s->ga.scales[1] == 3);

  // Gone: nobody keeps it.
  s->host->SendNpcGone({.npc = 1, .reason = proto::kGoneDead});
  REQUIRE(s->Until([&] { return !s->b->NpcScales().contains(1); }));
  CHECK_FALSE(s->a->NpcScales().contains(1));
  s->For(400ms);
  CHECK(s->b->Checks().Requests() == 0);
}

TEST_CASE("a handed-over NPC keeps its scale until its new owner says its own") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 40);
  s->h.Announce(1, {30.f, 0.f, 0.f});
  CHECK(s->host->SendCombat(proto::NpcScale{.npc = 1, .players = 2}));
  REQUIRE(s->Until([&] { return s->ga.scales[1] == 2; }));
  // The host walks off: the guest runs it, at the share of health it had.
  Place(*s->host, -60);
  REQUIRE(s->Until([&] { return s->a->OwnsNpc(1); }));
  CHECK(s->ga.took.at(1).health == doctest::Approx(0.4f));
  CHECK(s->a->NpcScales().at(1).players == 2);
  // The old owner's word no longer counts; the new owner's does.
  CHECK_FALSE(s->host->SendCombat(proto::NpcScale{.npc = 1, .players = 4}));
  CHECK(s->a->SendCombat(proto::NpcScale{.npc = 1, .players = 1}));
  REQUIRE(s->Until([&] { return s->h.scales[1] == 1; }));
  CHECK(s->host->NpcScales().at(1).players == 1);
}

// ---- the death ledger (docs/architecture/regions.md §14) ----

namespace {
constexpr std::uint64_t kGuard = 0x1111'2222'3333'4444ull, kGanger = 0x5555'6666'7777'8888ull,
                        kClerk = 0x9999'AAAA'BBBB'CCCCull;

// One of `game`'s NPCs, placed in the world under `entity`.
proto::NpcId Placed(Game &game, std::uint32_t counter, std::uint64_t entity, proto::Vec3 at) {
  game.Announce(counter, at);
  const auto npc = MakeNpcId(game.endpoint->You(), counter);
  CHECK(game.endpoint->SendCombat(proto::NpcPlaced{.npc = npc, .entity = entity}));
  return npc;
}
} // namespace

TEST_CASE("a placed NPC is named to the players that see it, early or late, by its owner only") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 20);
  Place(*s->b, 1000);
  REQUIRE(s->Until([&] { return s->b->Apart() && s->host->LinkedWith(s->a->You()); }));
  // Said before its spawn (the game's two queues): kept, said right after it.
  const auto early = MakeNpcId(s->a->You(), 1);
  CHECK_FALSE(s->a->SendCombat(proto::NpcPlaced{.npc = early, .entity = kGuard}));
  s->ga.Announce(1, {20.f, 0.f, 0.f});
  CHECK(s->a->PlacedNpcs().contains(early));
  const auto ganger = Placed(s->ga, 2, kGanger, {22.f, 0.f, 0.f});
  CHECK(s->a->SendCombat(proto::NpcPlaced{.npc = ganger, .entity = kGanger})); // said once
  // Not ours to say: another's NPC, an NPC nobody shares, a death.
  CHECK_FALSE(s->host->SendCombat(proto::NpcPlaced{.npc = ganger, .entity = kGanger}));
  CHECK_FALSE(s->a->SendCombat(proto::NpcPlaced{.npc = 99, .entity = 5}));
  CHECK_FALSE(s->a->SendCombat(proto::NpcDeath{.npc = ganger, .entity = kGanger}));
  REQUIRE(s->Until([&] { return s->h.placed.size() == 2; }));
  s->For(100ms);
  CHECK(s->h.placed.at(early) == kGuard);
  CHECK(s->h.placed.at(ganger) == kGanger);
  CHECK(s->host->PlacedNpcs().size() == 2);
  // The player apart sees neither the NPCs nor their names.
  CHECK(s->gb.placed.empty());
  CHECK(s->b->PlacedNpcs().empty());
  // It comes over: the names come with the NPCs.
  Place(*s->b, 40);
  REQUIRE(s->Until([&] { return s->gb.placed.size() == 2 && s->gb.proxies.size() == 2; }));
  CHECK(s->gb.placed.at(ganger) == kGanger);
  // One that leaves range takes its name with it.
  s->a->SendNpcGone({.npc = early, .reason = proto::kGoneAway});
  REQUIRE(s->Until([&] {
    return !s->host->PlacedNpcs().contains(early) && !s->b->PlacedNpcs().contains(early);
  }));
  CHECK(s->host->Deaths().Empty());
}

TEST_CASE("a placed NPC a player kills alone is dead for everyone, wherever they are") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 1000);
  Place(*s->b, 2000);
  REQUIRE(s->Until([&] { return s->a->Apart() && s->b->Apart() && s->host->Apart(); }));
  // The host, alone: a guard at its post, a ganger, and a passer-by of the
  // crowd (never said placed).
  const auto guard = Placed(s->h, 1, kGuard, {5.f, 0.f, 0.f});
  const auto ganger = Placed(s->h, 2, kGanger, {6.f, 0.f, 0.f});
  s->h.Announce(3, {7.f, 0.f, 0.f});
  s->h.Move(guard, {9.f, 2.f, 0.f}); // it fought, and fell there
  s->host->SendNpcGone({.npc = guard, .reason = proto::kGoneDead, .killer = s->host->You()});
  s->host->SendNpcGone({.npc = 3, .reason = proto::kGoneDead, .killer = s->host->You()});
  REQUIRE(s->Until([&] {
    return s->a->Deaths().Has(kGuard) && s->b->Deaths().Has(kGuard) && !s->h.deaths.empty();
  }));
  s->For(150ms);
  // Once each, the host's own game included; the crowd's death is nobody's.
  for (auto *game : s->Games()) {
    REQUIRE(game->deaths.size() == 1);
    const auto &death = game->deaths.front();
    CHECK(death.entity == kGuard);
    CHECK(death.npc == 0);
    CHECK(death.by == s->host->You());
    CHECK(death.order == 1);
    CHECK(death.timeUs != 0);
    CHECK(death.record == 101);
    CHECK(death.position == proto::Vec3{9.f, 2.f, 0.f});
    CHECK(death.flags == 0);
  }
  // The guests never saw the NPC: nothing else of it reached them.
  CHECK(s->ga.proxies.empty());
  CHECK(s->ga.placed.empty());

  // A guest, alone: knocked out counts (it never gets up), and its later
  // death is not a second one.
  const auto clerk = Placed(s->ga, 1, kClerk, {1001.f, 0.f, 0.f});
  s->a->SendNpcGone({.npc = clerk, .reason = proto::kGoneKnockedOut, .killer = s->a->You()});
  REQUIRE(s->Until([&] { return s->b->Deaths().Has(kClerk) && s->ga.deaths.size() == 2; }));
  s->a->SendNpcGone({.npc = clerk, .reason = proto::kGoneDead, .killer = s->a->You()});
  s->For(150ms);
  for (auto *game : s->Games()) {
    REQUIRE(game->deaths.size() == 2);
    CHECK(game->deaths.back().entity == kClerk);
    CHECK(game->deaths.back().by == s->a->You());
    CHECK(game->deaths.back().order == 2);
    CHECK(game->deaths.back().flags == proto::kDeathKnockedOut);
    CHECK(game->deaths.back().position == proto::Vec3{1001.f, 0.f, 0.f});
  }
  // One that only leaves range is not dead.
  s->host->SendNpcGone({.npc = ganger, .reason = proto::kGoneAway});
  s->For(150ms);
  CHECK(s->host->Deaths().Size() == 2);
  CHECK_FALSE(s->a->Deaths().Has(kGanger));
  // Everyone's ledger is the host's.
  CHECK(s->a->Deaths().Digest() == s->host->Deaths().Digest());
  CHECK(s->b->Deaths().Digest() == s->host->Deaths().Digest());
}

TEST_CASE("a late joiner hears every death of the session, in order, once") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 1000);
  REQUIRE(s->Until([&] { return s->a->Apart(); }));
  const auto guard = Placed(s->h, 1, kGuard, {5.f, 0.f, 0.f});
  const auto clerk = Placed(s->ga, 1, kClerk, {1001.f, 0.f, 0.f});
  s->For(50ms);
  s->host->SendNpcGone({.npc = guard, .reason = proto::kGoneDead});
  REQUIRE(s->Until([&] { return s->a->Deaths().Has(kGuard); }));
  s->a->SendNpcGone({.npc = clerk, .reason = proto::kGoneDead, .killer = s->a->You()});
  REQUIRE(s->Until([&] { return s->host->Deaths().Size() == 2; }));
  std::string error;
  s->b = Endpoint::Join({"late"}, "127.0.0.1", s->host->LocalPort(), error, kQuick);
  REQUIRE_MESSAGE(s->b, error);
  s->gb.endpoint = s->b.get();
  REQUIRE(s->Until([&] { return s->b->IsJoined() && s->gb.deaths.size() == 2; }));
  s->For(400ms); // a few digests: nothing is asked again, nothing said twice
  REQUIRE(s->gb.deaths.size() == 2);
  CHECK(s->gb.deaths[0].entity == kGuard);
  CHECK(s->gb.deaths[1].entity == kClerk);
  CHECK(s->gb.deaths[1].by == s->a->You());
  CHECK(s->b->Deaths().Digest() == s->host->Deaths().Digest());
  CHECK(s->ga.deaths.size() == 2);
}

TEST_CASE("a death the host's word of was lost is healed by the world digest") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 1000);
  REQUIRE(s->Until([&] { return s->a->Apart(); }));
  const auto guard = Placed(s->h, 1, kGuard, {5.f, 0.f, 0.f});
  const auto ganger = Placed(s->h, 2, kGanger, {6.f, 0.f, 0.f});
  s->For(50ms);
  // Everything the host says is lost while its V kills the guard.
  s->host->SetFaults({.drop = 1.0});
  s->host->SendNpcGone({.npc = guard, .reason = proto::kGoneDead});
  s->For(50ms);
  s->host->SetFaults({});
  CHECK(s->host->Deaths().Has(kGuard));
  CHECK_FALSE(s->a->Deaths().Has(kGuard));
  // Two digests that differ, then the world again: the death with it.
  REQUIRE(s->Until([&] { return s->a->Deaths().Has(kGuard); }));
  CHECK(s->a->Checks().Requests() >= 1);
  REQUIRE(s->ga.deaths.size() == 1);
  // The next one arrives as usual, and the resync told nothing twice.
  s->host->SendNpcGone({.npc = ganger, .reason = proto::kGoneDead});
  REQUIRE(s->Until([&] { return s->ga.deaths.size() == 2; }));
  s->For(300ms);
  CHECK(s->ga.deaths.size() == 2);
  CHECK(s->a->Deaths().Digest() == s->host->Deaths().Digest());
}

TEST_CASE("the ledger outlives its reporter; an heir's kill of a placed NPC is the heir's") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 30);
  Place(*s->b, 1000);
  REQUIRE(s->Until([&] { return s->b->Apart() && s->host->LinkedWith(s->a->You()); }));
  const auto owner = s->a->You();
  const auto guard = Placed(s->ga, 1, kGuard, {10.f, 0.f, 0.f});
  const auto clerk = Placed(s->ga, 2, kClerk, {12.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->h.proxies.size() == 2 && s->h.placed.size() == 2; }));
  s->a->SendNpcGone({.npc = guard, .reason = proto::kGoneDead, .killer = owner});
  REQUIRE(s->Until([&] { return s->b->Deaths().Has(kGuard); }));
  s->For(100ms);

  s->a.reset(); // no goodbye
  REQUIRE(
      s->Until([&] { return s->host->Players().size() == 2 && s->b->Players().size() == 2; }, 4s));
  // Its kill stays in the ledger, under its name.
  REQUIRE(s->host->Deaths().Find(kGuard) != nullptr);
  CHECK(s->host->Deaths().Find(kGuard)->by == owner);
  CHECK(s->b->Deaths().Has(kGuard));
  // The host inherited the clerk next to it, name and all: its death there
  // is the host's report.
  REQUIRE(s->Until([&] { return s->h.running.contains(clerk); }));
  CHECK(s->host->PlacedNpcs().contains(clerk));
  s->host->SendNpcGone({.npc = clerk, .reason = proto::kGoneDead, .killer = s->host->You()});
  REQUIRE(s->Until([&] { return s->b->Deaths().Has(kClerk); }));
  CHECK(s->b->Deaths().Find(kClerk)->by == s->host->You());
  CHECK(s->b->Deaths().Find(kClerk)->order == 2);
}
