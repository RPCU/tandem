#include "tandem/sim/world.hpp"

#include "tandem/combat.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace tandem::sim {
namespace {
namespace p = proto;

// The game's enums (docs/research/engine-knowledge.md §13.21).
constexpr std::uint8_t kRelaxed = 5, kCombat = 2, kAlerted = 0; // gamedataNPCHighLevelState
constexpr std::uint8_t kNormal = 6, kAim = 0, kShoot = 9;       // gamedataNPCUpperBodyState
constexpr std::uint8_t kStand = 3, kCrouch = 2, kInCover = 1;   // gamedataNPCStanceState
constexpr std::uint8_t kRanged = 10, kMeleeAttack = 7;          // gamedataAttackType
constexpr std::uint8_t kSemiAuto = 4;                           // gamedataTriggerMode
constexpr std::uint8_t kFxShoot = 16;                           // gamedataFxAction

constexpr float kSight = 28.f, kCombatSight = 45.f;
constexpr std::uint64_t kLoseUs = 8'000'000;    // unseen this long: lost
constexpr std::uint64_t kEffectUs = 10'000'000; // a status effect lasts
constexpr std::uint64_t kPoiseBackUs = 4'000'000;

const std::uint64_t kBlind = TweakDBID("BaseStatusEffect.Blind");
const std::uint64_t kStun = TweakDBID("BaseStatusEffect.Stun");
const std::uint64_t kEmp = TweakDBID("BaseStatusEffect.EMP");
const std::uint64_t kUnconscious = TweakDBID("BaseStatusEffect.Unconscious");

constexpr const char *kWeather[] = {
    "24h_weather_sunny",        "24h_weather_light_clouds", "24h_weather_cloudy",
    "24h_weather_heavy_clouds", "24h_weather_fog",          "24h_weather_rain",
    "24h_weather_toxic_rain",   "24h_weather_pollution",    "24h_weather_sandstorm"};

std::uint8_t Bit(p::PlayerId player) {
  return player >= 1 && player <= 8 ? static_cast<std::uint8_t>(1u << (player - 1)) : 0;
}

bool Fights(const SimNpc &npc) { return npc.mode == NpcMode::Combat || npc.mode == NpcMode::Cover; }
} // namespace

const char *NameOf(NpcMode mode) {
  constexpr const char *names[] = {"idle",      "patrol", "alerted", "combat",      "cover",
                                   "staggered", "flee",   "held",    "unconscious", "dead"};
  return names[static_cast<std::size_t>(mode)];
}

std::vector<NpcSpec> HostWorld::DefaultCast() {
  const std::string ranged = "Character.kab_tyger_claws_biker1_ranged1_saratoga_ma";
  const std::string melee = "Character.rcr_sixthstreet_hooligan_melee1_knife_wa";
  const std::string gun = "Items.Preset_Lexington_Default";
  return {
      {NpcKind::Ranged, ranged, gun, 100.f, 100.f, ""},
      {NpcKind::Melee, melee, "Items.Preset_Katana_Default", 120.f, 80.f, ""},
      {NpcKind::Ranged, ranged, gun, 100.f, 100.f, "Items.Preset_Overture_Kerry"},
      {NpcKind::Netrunner, ranged, gun, 80.f, 60.f, ""},
      {NpcKind::Civilian, melee, "", 60.f, 40.f, ""},
  };
}

HostWorld::HostWorld(Config config, Random random, Coverage *coverage)
    : m_config(std::move(config)), m_random(random), m_coverage(coverage) {
  if (m_config.cast.empty())
    m_config.cast = DefaultCast();
  m_gameTime = m_config.startTime;
  m_gameSeconds = m_config.startTime;
  m_weather = CName(kWeather[0]);
  if (m_coverage)
    for (const char *feature : {"npc.spawn",
                                "npc.despawn",
                                "npc.patrol",
                                "npc.notice",
                                "npc.detect",
                                "npc.lose",
                                "npc.hear",
                                "npc.combat",
                                "npc.cover",
                                "npc.flee",
                                "npc.shoot-guest",
                                "npc.shoot-host",
                                "npc.melee-guest",
                                "npc.melee-host",
                                "npc.hack-guest",
                                "npc.hit",
                                "npc.hit-ignored",
                                "npc.poise-broken",
                                "npc.knockout",
                                "npc.death",
                                "npc.wound",
                                "npc.kill-capped",
                                "npc.status-effect",
                                "npc.status-expired",
                                "npc.upload",
                                "npc.upload-failed",
                                "npc.grab",
                                "npc.break-free",
                                "npc.release",
                                "npc.takedown",
                                "npc.takedown-knockout",
                                "npc.finisher",
                                "npc.carry",
                                "npc.drop",
                                "npc.throw",
                                "npc.dispose",
                                "npc.refuse-gone",
                                "npc.refuse-held",
                                "npc.refuse-conditions",
                                "npc.refuse-not-yours",
                                "npc.refuse-too-far",
                                "npc.freed-on-leave",
                                "npc.time-bubble",
                                "npc.corpse-removed",
                                "npc.quest-item",
                                "npc.wave",
                                "npc.shot-hit-reported",
                                "world.time",
                                "world.weather",
                                "world.paused",
                                "world.rebuilt",
                                "world.difficulty"})
      m_coverage->Declare(feature);
}

SimNpc *HostWorld::Find(p::NpcId npc) {
  const auto it = m_npcs.find(npc);
  return it == m_npcs.end() ? nullptr : &it->second;
}

std::size_t HostWorld::Alive() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(m_npcs, [](const auto &entry) { return entry.second.Alive(); }));
}

void HostWorld::Fill(Scene &scene) const {
  scene.npcs.clear();
  for (const auto &[id, npc] : m_npcs)
    scene.npcs[id] = SeenNpc{.id = id,
                             .record = TweakDBID(npc.spec.record),
                             .position = npc.position,
                             .yaw = npc.yaw,
                             .health = npc.spec.health > 0.f ? npc.health / npc.spec.health : 0.f,
                             .live = npc.Alive(),
                             .body = npc.body,
                             .dead = npc.mode == NpcMode::Dead,
                             .holder = npc.holder,
                             .detected = npc.detected};
}

void HostWorld::Spawn(std::uint64_t nowUs, const p::Vec3 &anchor, Outbox &out, bool fighter) {
  SimNpc npc;
  npc.id = m_nextNpc++;
  npc.spec = m_config.cast[(npc.id - 1) % m_config.cast.size()];
  if (fighter && npc.spec.kind == NpcKind::Civilian)
    npc.spec = m_config.cast.front();
  const float angle = m_random.Between(0.f, 2.f * kPi);
  const float range = m_config.ring * m_random.Between(0.7f, 1.3f);
  npc.position = anchor + p::Vec3{std::cos(angle) * range, std::sin(angle) * range, 0.f};
  npc.home = npc.goal = npc.position;
  npc.yaw = YawTo(npc.position, anchor);
  npc.health = npc.spec.health;
  npc.poise = npc.spec.poise;
  npc.mode = m_random.Chance(0.5) ? NpcMode::Patrol : NpcMode::Idle;
  npc.modeUntilUs = nowUs + static_cast<std::uint64_t>(m_random.Between(3.f, 9.f) * 1e6f);
  npc.anim = {.npc = npc.id};
  out.push_back({p::NpcSpawn{.npc = npc.id,
                             .record = TweakDBID(npc.spec.record),
                             .appearance = 0,
                             .position = npc.position,
                             .yaw = npc.yaw}});
  out.push_back({p::NpcHealth{.npc = npc.id, .health = 1.f}});
  m_npcs[npc.id] = std::move(npc);
  Hit("npc.spawn");
}

void HostWorld::Remove(SimNpc &npc, std::uint8_t reason, Outbox &out) {
  out.push_back({p::NpcGone{.npc = npc.id, .reason = reason, .killer = npc.killer}});
  std::erase_if(m_uploads, [&](const Upload &upload) {
    if (upload.state.npc != npc.id)
      return false;
    auto failed = upload.state;
    failed.step = p::HackStep::Failed;
    out.push_back({failed});
    Hit("npc.upload-failed");
    return true;
  });
}

void HostWorld::Free(SimNpc &npc, Outbox &out) {
  if (npc.holder == p::kNoPlayer)
    return;
  npc.holder = p::kNoPlayer;
  npc.hold = p::NpcHoldKind::None;
  out.push_back({p::NpcHold{.npc = npc.id}});
}

// It falls: knocked out, or dead. Its body stays (NpcGone says which).
void HostWorld::Fall(SimNpc &npc, bool dead, p::PlayerId by, std::uint64_t nowUs, Outbox &out) {
  if (npc.mode == NpcMode::Dead || (!dead && npc.mode == NpcMode::Unconscious))
    return;
  const bool wasBody = npc.body;
  npc.mode = dead ? NpcMode::Dead : NpcMode::Unconscious;
  npc.body = true;
  npc.killer = by;
  npc.detected = 0;
  npc.target = p::kNoPlayer;
  npc.awareness.clear();
  npc.goneAtUs = nowUs;
  if (dead)
    npc.health = 0.f;
  for (const auto &[effect, state] : npc.effects)
    out.push_back({p::NpcStatus{.npc = npc.id, .player = state.player, .effect = effect}});
  npc.effects.clear();
  if (!dead)
    out.push_back({p::NpcHealth{.npc = npc.id, .health = 0.001f}});
  Remove(npc, dead ? p::kGoneDead : p::kGoneKnockedOut, out);
  Hit(dead ? "npc.death" : "npc.knockout");
  // What it carried for the story is every player's.
  if (dead && !wasBody && !npc.spec.questItem.empty()) {
    out.push_back(
        {p::SharedItem{.kind = p::kSharedOnCorpse,
                       .npc = npc.id,
                       .item = {.record = TweakDBID(npc.spec.questItem), .quantity = 1}}});
    Hit("npc.quest-item");
  }
}

void HostWorld::Clock(std::uint64_t nowUs, const Scene &scene, Outbox &out) {
  const bool paused =
      std::ranges::any_of(scene.players, [](const auto &entry) { return entry.second.paused; });
  bool changed = paused != m_paused;
  if (changed && paused)
    Hit("world.paused");
  m_paused = paused;
  if (nowUs >= m_nextWeatherUs) {
    if (m_nextWeatherUs != 0) {
      m_weather = CName(kWeather[m_random.Index(std::size(kWeather))]);
      changed = true;
      Hit("world.weather");
    }
    m_nextWeatherUs = nowUs + static_cast<std::uint64_t>(static_cast<float>(m_config.weatherUs) *
                                                         m_random.Between(0.6f, 1.4f));
  }
  if (!changed && nowUs < m_nextWorldUs)
    return;
  m_nextWorldUs = nowUs + 10'000'000;
  m_gameTime = static_cast<std::int32_t>(m_gameSeconds);
  out.push_back(
      {p::World{.gameTimeSeconds = m_gameTime, .weather = m_weather, .timePaused = m_paused}});
  Hit("world.time");
}

void HostWorld::Tick(std::uint64_t nowUs, const Scene &scene, const SeenPlayer &self, Outbox &out) {
  m_self = self.id;
  if (!m_started) {
    m_started = true;
    m_startUs = m_lastUs = nowUs;
    out.push_back({p::Difficulty{m_config.difficulty}});
    Hit("world.difficulty");
  }
  const float dt = std::min(static_cast<float>(nowUs - m_lastUs) / 1e6f, 0.5f);
  m_lastUs = nowUs;
  if (!m_paused)
    m_gameSeconds += static_cast<double>(dt) * m_config.clockScale;
  Clock(nowUs, scene, out);
  if (m_paused)
    return; // the pause menu pauses everyone's world

  // Who the world is around: the partner's V (the real game's, so its
  // player sees the NPCs), or our own.
  const auto *partner = scene.Partner();
  const p::Vec3 anchor =
      m_config.aroundSelf || !partner || !partner->Present() ? self.position : partner->position;
  const bool anyone = self.heard || (partner && partner->heard);

  // The first group at once; then, a while after the last fighter fell, a
  // new one.
  const bool fighters = std::ranges::any_of(m_npcs, [](const auto &entry) {
    return entry.second.Alive() && entry.second.spec.kind != NpcKind::Civilian;
  });
  if (fighters) {
    m_waveAtUs = 0;
  } else if (anyone) {
    if (m_waveAtUs == 0)
      m_waveAtUs = nowUs + (m_everSpawned ? m_config.waveUs : 0);
    if (nowUs >= m_waveAtUs) {
      if (m_everSpawned)
        Hit("npc.wave");
      m_everSpawned = true;
      m_waveAtUs = 0;
      Spawn(nowUs, anchor, out, true);
      for (auto count = Alive(); count < m_config.npcs; ++count)
        Spawn(nowUs, anchor, out, false);
    }
  }

  // Time bubbles: the NPCs around a V whose ability is on are slowed.
  std::vector<sync::TimeBubbles::Owner> owners;
  const auto bubbleOf = [&](const SeenPlayer &player) {
    if ((player.abilities & p::kTimeBubbleAbilities) && player.timeScale < 1.f && player.heard)
      owners.push_back({player.id, player.timeScale, player.position});
  };
  bubbleOf(self);
  for (const auto &[id, player] : scene.players)
    bubbleOf(player);
  std::vector<sync::TimeBubbles::Npc> inWorld;
  for (const auto &[id, npc] : m_npcs)
    if (npc.Alive())
      inWorld.push_back({id, npc.position});
  for (const auto &change : m_bubbles.Update(owners, inWorld))
    if (auto *npc = Find(change.npc)) {
      npc->factor = change.factor;
      if (change.factor < 1.f)
        Hit("npc.time-bubble");
    }

  // Uploads that are done, takedowns that end.
  for (auto it = m_uploads.begin(); it != m_uploads.end();) {
    if (nowUs < it->doneUs) {
      ++it;
      continue;
    }
    auto done = it->state;
    done.step = p::HackStep::Done;
    out.push_back({done});
    if (auto *npc = Find(done.npc); npc && npc->Alive() && (it->flags & p::kUploadTraceable)) {
      // A traced hack tells it where its netrunner is.
      npc->awareness[done.player] = std::max(npc->awareness[done.player], 0.7f);
      if (npc->mode == NpcMode::Idle || npc->mode == NpcMode::Patrol)
        npc->mode = NpcMode::Alerted;
    }
    Hit("npc.upload");
    it = m_uploads.erase(it);
  }
  for (auto it = m_endings.begin(); it != m_endings.end();) {
    if (nowUs < it->atUs) {
      ++it;
      continue;
    }
    const auto ending = *it;
    it = m_endings.erase(it);
    auto *npc = Find(ending.npc);
    if (!npc)
      continue;
    Free(*npc, out);
    if (ending.part >= 0)
      out.push_back({p::NpcWound{.npc = npc->id,
                                 .player = ending.by,
                                 .part = static_cast<std::uint8_t>(ending.part),
                                 .wound = 0,
                                 .position = npc->position}});
    if (ending.by != p::kNoPlayer)
      Fall(*npc, ending.dead, ending.by, nowUs, out);
    else if (npc->Alive())
      npc->mode = NpcMode::Alerted;
  }

  std::vector<p::NpcId> gone;
  for (auto &[id, npc] : m_npcs) {
    Think(npc, dt, nowUs, scene, self, out);
    // Bodies are cleared after a while; the living that everyone left go.
    if (npc.body && npc.holder == p::kNoPlayer && nowUs - npc.goneAtUs > m_config.corpseUs) {
      out.push_back({p::NpcGone{.npc = id, .reason = p::kGoneAway}});
      gone.push_back(id);
      Hit("npc.corpse-removed");
    } else if (npc.Alive() && npc.holder == p::kNoPlayer) {
      float nearest = Distance(npc.position, self.position);
      for (const auto &[player, seen] : scene.players)
        if (seen.heard)
          nearest = std::min(nearest, Distance(npc.position, seen.position));
      if (nearest > m_config.leash) {
        Remove(npc, p::kGoneAway, out);
        gone.push_back(id);
        Hit("npc.despawn");
      }
    }
  }
  for (const auto id : gone)
    m_npcs.erase(id);

  if (nowUs >= m_nextStatesUs) {
    m_nextStatesUs = nowUs + 100'000;
    p::NpcStates states;
    p::NpcAnims anims;
    for (auto &[id, npc] : m_npcs) {
      if (!npc.Alive())
        continue;
      if (states.npcs.size() < p::kMaxNpcsPerState)
        states.npcs.push_back(
            {.npc = id, .position = npc.position, .yaw = npc.yaw, .detected = npc.detected});
      Animate(npc, anims);
    }
    if (!states.npcs.empty())
      out.push_back({std::move(states)});
    if (!anims.npcs.empty())
      out.push_back({std::move(anims)});
  }
  if (nowUs >= m_nextAwarenessUs) {
    m_nextAwarenessUs = nowUs + 200'000;
    p::NpcAwareness awareness;
    for (const auto &[id, npc] : m_npcs)
      for (const auto &[player, level] : npc.awareness)
        if (player != m_self && level > 0.f && npc.Alive() &&
            awareness.entries.size() < p::kMaxNpcAwareness)
          awareness.entries.push_back(
              {id, player, static_cast<std::uint8_t>(std::clamp(level, 0.f, 1.f) * 255.f)});
    // Nobody noticed any more is said once, with an empty list.
    if (!awareness.entries.empty() || m_awarenessShown) {
      m_awarenessShown = !awareness.entries.empty();
      out.push_back({std::move(awareness)});
    }
  }
}

void HostWorld::Animate(SimNpc &npc, p::NpcAnims &changed) {
  p::NpcAnim anim{.npc = npc.id};
  const bool armed = !npc.spec.weapon.empty();
  switch (npc.mode) {
  case NpcMode::Combat:
    anim.highLevel = kCombat;
    anim.upperBody = npc.burst > 0 ? kShoot : kAim;
    anim.stance = kStand;
    break;
  case NpcMode::Cover:
    anim.highLevel = kCombat;
    anim.stance = kInCover;
    break;
  case NpcMode::Alerted:
  case NpcMode::Flee:
    anim.highLevel = kAlerted;
    break;
  case NpcMode::Staggered:
    anim.highLevel = kCombat;
    anim.stance = kCrouch;
    break;
  default:
    anim.highLevel = kRelaxed;
    anim.upperBody = kNormal;
    anim.stance = kStand;
    break;
  }
  if (armed && Fights(npc)) {
    anim.flags = p::kNpcAnimWeaponDrawn;
    anim.weapon = TweakDBID(npc.spec.weapon);
    anim.target = npc.target;
  }
  if (anim == npc.anim)
    return;
  npc.anim = anim;
  if (changed.npcs.size() < p::kMaxNpcAnims)
    changed.npcs.push_back(anim);
}

void HostWorld::Detect(SimNpc &npc, p::PlayerId player, std::uint64_t nowUs, Outbox &out) {
  npc.awareness[player] = 1.f;
  npc.seenUs[player] = nowUs;
  if (npc.detected & Bit(player))
    return;
  npc.detected |= Bit(player);
  Hit("npc.detect");
  if (npc.spec.kind == NpcKind::Civilian) {
    npc.mode = NpcMode::Flee;
    npc.modeUntilUs = nowUs + 10'000'000;
    Hit("npc.flee");
    return;
  }
  if (!Fights(npc) && npc.mode != NpcMode::Staggered && npc.mode != NpcMode::Held) {
    npc.mode = NpcMode::Combat;
    npc.nextShotUs = nowUs + 600'000;
    npc.nextCoverUs = nowUs + static_cast<std::uint64_t>(m_random.Between(4.f, 8.f) * 1e6f);
    npc.nextHackUs = nowUs + static_cast<std::uint64_t>(m_random.Between(4.f, 9.f) * 1e6f);
    Hit("npc.combat");
  }
  if (npc.target == p::kNoPlayer)
    npc.target = player;
  // Its game plays "the player's" half of the reaction.
  if (player != m_self)
    out.push_back({p::NpcCue{.npc = npc.id, .player = player, .cue = p::NpcCueKind::CombatStart}});
}

void HostWorld::Perceive(SimNpc &npc, float dt, std::uint64_t nowUs, const Scene &scene,
                         const SeenPlayer &self, Outbox &out) {
  const bool blind = npc.effects.contains(kBlind);
  const auto look = [&](const SeenPlayer &player) {
    const float distance = Distance(npc.position, player.position);
    const bool fighting = Fights(npc);
    const bool hidden =
        !player.Present() || player.life != p::Life::Up || (player.stealth & p::kStealthCamo) ||
        player.transit == p::TransitKind::Metro || player.transit == p::TransitKind::Along;
    const bool sees = !blind && !hidden && distance < (fighting ? kCombatSight : kSight);
    auto &level = npc.awareness[player.id];
    const bool detected = npc.detected & Bit(player.id);
    if (sees) {
      npc.seenUs[player.id] = nowUs;
      float rate = std::max(1.3f - distance / kSight, 0.1f) * std::max(player.visibility, 0.05f);
      if (player.stealth & p::kStealthCrouch)
        rate *= 0.4f;
      if (level == 0.f && rate > 0.f)
        Hit("npc.notice");
      level = std::min(level + rate * dt * npc.factor, 1.f);
      if (level >= 1.f && !detected)
        Detect(npc, player.id, nowUs, out);
    } else if (!detected) {
      level = std::max(level - 0.25f * dt, 0.f);
    } else if (nowUs - npc.seenUs[player.id] > kLoseUs) {
      // Lost: it looks for them a while, then forgets.
      npc.detected &= static_cast<std::uint8_t>(~Bit(player.id));
      level = 0.5f;
      Hit("npc.lose");
      if (npc.target == player.id) {
        npc.target = p::kNoPlayer;
        for (p::PlayerId other = 1; other <= 8; ++other)
          if (npc.detected & Bit(other))
            npc.target = other;
        if (npc.target == p::kNoPlayer && Fights(npc)) {
          npc.mode = NpcMode::Alerted;
          npc.modeUntilUs = nowUs + 6'000'000;
          npc.goal = player.position;
        }
      }
    }
  };
  look(self);
  for (const auto &[id, player] : scene.players)
    look(player);
  // Whoever left is forgotten.
  std::erase_if(npc.awareness, [&](const auto &entry) {
    return entry.first != self.id && !scene.players.contains(entry.first);
  });
}

void HostWorld::Fire(SimNpc &npc, const SeenPlayer &target, std::uint64_t nowUs, Outbox &out) {
  const auto weapon = TweakDBID(npc.spec.weapon);
  if (target.id != m_self) {
    // The guest's game fires it from its proxy and decides the hit; the
    // cosmetic shot goes to everyone else (the endpoint pairs the two).
    out.push_back({p::NpcShotAt{.player = target.id,
                                .npc = npc.id,
                                .weapon = weapon,
                                .tbh = 1.f,
                                .trigger = kSemiAuto,
                                .factor = npc.factor}});
    Hit("npc.shoot-guest");
  } else {
    Hit("npc.shoot-host");
    if (hitOwnV && m_random.Chance(0.3))
      hitOwnV({.player = m_self,
               .npc = npc.id,
               .physical = 9.f,
               .attackType = kRanged,
               .position = target.position,
               .direction = Forward(npc.yaw),
               .weapon = weapon});
  }
  out.push_back({p::NpcShot{.npc = npc.id, .effect = kFxShoot, .target = target.position}});
  (void)nowUs;
}

void HostWorld::Melee(SimNpc &npc, const SeenPlayer &target, Outbox &out) {
  const p::PlayerHit hit{.player = target.id,
                         .npc = npc.id,
                         .physical = 16.f,
                         .attackType = kMeleeAttack,
                         .flags = p::kHitMelee,
                         .position = target.position,
                         .direction = Forward(npc.yaw),
                         .weapon = TweakDBID(npc.spec.weapon)};
  if (target.id == m_self) {
    Hit("npc.melee-host");
    if (hitOwnV)
      hitOwnV(hit);
  } else {
    out.push_back({hit});
    Hit("npc.melee-guest");
  }
}

void HostWorld::Think(SimNpc &npc, float dt, std::uint64_t nowUs, const Scene &scene,
                      const SeenPlayer &self, Outbox &out) {
  if (npc.body)
    return;
  // Status effects that ran out leave it, for everyone.
  for (auto it = npc.effects.begin(); it != npc.effects.end();) {
    if (nowUs < it->second.untilUs) {
      ++it;
      continue;
    }
    out.push_back({p::NpcStatus{.npc = npc.id, .player = it->second.player, .effect = it->first}});
    Hit("npc.status-expired");
    it = npc.effects.erase(it);
  }
  if (npc.poise < npc.spec.poise && nowUs >= npc.poiseBackUs) {
    npc.poise = npc.spec.poise;
    out.push_back({p::NpcPoise{.npc = npc.id, .poise = 1.f}});
  }
  const auto playerOf = [&](p::PlayerId id) -> const SeenPlayer * {
    return id == self.id ? &self : scene.Find(id);
  };
  const auto walk = [&](const p::Vec3 &to, float speed) {
    const float distance = Distance(npc.position, to);
    if (distance < 0.05f)
      return;
    npc.yaw = YawTo(npc.position, to);
    const float step = std::min(speed * npc.factor * dt, distance);
    npc.position = npc.position + (to - npc.position) * (step / distance);
    npc.position.z = to.z;
  };

  if (npc.mode == NpcMode::Held) {
    // In its holder's arms; a grapple left alone breaks free.
    const auto *holder = playerOf(npc.holder);
    if (holder && holder->heard)
      npc.position = holder->position + Forward(holder->yaw) * 0.6f;
    if (npc.hold == p::NpcHoldKind::Grappled && nowUs - npc.heldSinceUs > m_config.grappleUs) {
      const auto by = npc.holder;
      Free(npc, out);
      npc.mode = NpcMode::Idle;
      Detect(npc, by, nowUs, out);
      Hit("npc.break-free");
    }
    return;
  }
  if (npc.mode == NpcMode::Staggered) {
    if (nowUs < npc.modeUntilUs)
      return;
    npc.mode = npc.detected ? NpcMode::Combat : NpcMode::Alerted;
    npc.modeUntilUs = nowUs + 5'000'000;
  }
  Perceive(npc, dt, nowUs, scene, self, out);

  switch (npc.mode) {
  case NpcMode::Idle:
    if (nowUs >= npc.modeUntilUs) {
      npc.mode = NpcMode::Patrol;
      npc.goal = npc.home + p::Vec3{m_random.Between(-6.f, 6.f), m_random.Between(-6.f, 6.f), 0.f};
      Hit("npc.patrol");
    }
    break;
  case NpcMode::Patrol:
    walk(npc.goal, 1.3f);
    if (Distance(npc.position, npc.goal) < 0.3f) {
      npc.mode = NpcMode::Idle;
      npc.modeUntilUs = nowUs + static_cast<std::uint64_t>(m_random.Between(2.f, 7.f) * 1e6f);
    }
    break;
  case NpcMode::Alerted:
    walk(npc.goal, 1.8f);
    if (nowUs >= npc.modeUntilUs) {
      npc.mode = NpcMode::Idle;
      npc.modeUntilUs = nowUs + 3'000'000;
    }
    break;
  case NpcMode::Flee: {
    const auto *from = playerOf(npc.target ? npc.target : self.id);
    const p::Vec3 danger = from ? from->position : self.position;
    const float away = YawTo(danger, npc.position);
    walk(npc.position + Forward(away) * 5.f, 5.f);
    if (nowUs >= npc.modeUntilUs) {
      npc.mode = NpcMode::Idle;
      npc.detected = 0;
      npc.home = npc.position;
      npc.modeUntilUs = nowUs + 4'000'000;
    }
    break;
  }
  case NpcMode::Cover:
    if (nowUs >= npc.modeUntilUs) {
      npc.mode = NpcMode::Combat;
      npc.nextShotUs = nowUs + 400'000;
    }
    break;
  case NpcMode::Combat: {
    const auto *target = playerOf(npc.target);
    if (!target || !target->heard) {
      npc.target = p::kNoPlayer;
      npc.mode = NpcMode::Alerted;
      npc.modeUntilUs = nowUs + 5'000'000;
      break;
    }
    const float distance = Distance(npc.position, target->position);
    npc.yaw = YawTo(npc.position, target->position);
    const auto wait = [&](float low, float high) {
      return static_cast<std::uint64_t>(m_random.Between(low, high) * 1e6f / npc.factor);
    };
    if (npc.spec.kind == NpcKind::Melee) {
      if (distance > 1.6f)
        walk(target->position, 5.f);
      else if (nowUs >= npc.nextMeleeUs && target->life == p::Life::Up) {
        npc.nextMeleeUs = nowUs + wait(1.4f, 2.2f);
        Melee(npc, *target, out);
      }
      break;
    }
    // Ranged: keep its distance, fire in bursts, duck into cover now and then.
    if (distance > 16.f)
      walk(target->position, 3.5f);
    else if (distance < 7.f)
      walk(npc.position + Forward(YawTo(target->position, npc.position)) * 3.f, 3.f);
    if (nowUs >= npc.nextCoverUs && npc.health < npc.spec.health * 0.8f && npc.burst == 0) {
      npc.mode = NpcMode::Cover;
      npc.modeUntilUs = nowUs + wait(2.f, 4.f);
      npc.nextCoverUs = npc.modeUntilUs + wait(5.f, 10.f);
      Hit("npc.cover");
      break;
    }
    if (nowUs >= npc.nextShotUs && distance < kCombatSight && target->life == p::Life::Up) {
      if (npc.burst == 0)
        npc.burst = 3;
      Fire(npc, *target, nowUs, out);
      npc.nextShotUs = nowUs + (--npc.burst > 0 ? wait(0.15f, 0.2f) : wait(1.6f, 3.2f));
    }
    if (npc.spec.kind == NpcKind::Netrunner && nowUs >= npc.nextHackUs) {
      npc.nextHackUs = nowUs + wait(10.f, 16.f);
      if (target->id != m_self) {
        out.push_back({p::PlayerHack{.player = target->id,
                                     .npc = npc.id,
                                     .action = TweakDBID("AIQuickHack.HackRevealPosition"),
                                     .flags = p::kPlayerHackReveal}});
        Hit("npc.hack-guest");
      }
    }
    break;
  }
  default:
    break;
  }
}

void HostWorld::OnStrike(const p::NpcStrike &strike, std::uint64_t nowUs, Outbox &out) {
  auto *npc = Find(strike.npc);
  // A dead NPC ignores hits; one that is gone was never there to hit.
  if (!npc || npc->mode == NpcMode::Dead) {
    Hit("npc.hit-ignored");
    return;
  }
  const float before = npc->health;
  // An unconscious body takes lethal hits only (a later one kills it).
  if (npc->mode == NpcMode::Unconscious && (strike.flags & p::kStrikeNonlethal)) {
    Hit("npc.hit-ignored");
    return;
  }
  npc->health -= combat::StrikeLoss(strike, npc->health, npc->spec.health, false);
  Hit("npc.hit");
  if (strike.poise > 0.f && npc->Alive()) {
    npc->poise = std::max(npc->poise - strike.poise, 0.f);
    npc->poiseBackUs = nowUs + kPoiseBackUs;
    out.push_back({p::NpcPoise{.npc = npc->id, .poise = npc->poise / npc->spec.poise}});
    if (npc->poise <= 0.f && npc->mode != NpcMode::Held) {
      npc->mode = NpcMode::Staggered;
      npc->modeUntilUs = nowUs + 2'000'000;
      if (strike.player != m_self)
        out.push_back({p::NpcCue{
            .npc = npc->id, .player = strike.player, .cue = p::NpcCueKind::PoiseBroken}});
      Hit("npc.poise-broken");
    }
  }
  if (npc->health > 0.f) {
    if (npc->health != before)
      out.push_back({p::NpcHealth{.npc = npc->id, .health = npc->health / npc->spec.health}});
    if ((strike.flags & p::kStrikeLeaveOneHp) || strike.minHealth > 0.f)
      Hit("npc.kill-capped");
    // It turns on whoever hit it.
    if (npc->Alive() && npc->mode != NpcMode::Held)
      Detect(*npc, strike.player, nowUs, out);
    return;
  }
  if (strike.flags & p::kStrikeNonlethal) {
    Free(*npc, out);
    Fall(*npc, false, strike.player, nowUs, out);
    return;
  }
  // A blade, or a hit the attacker's game marked: the limb it landed on.
  if (strike.part >= 0 && strike.part <= 5) {
    const bool clean = strike.flags & p::kStrikeMelee;
    out.push_back({p::NpcWound{.npc = npc->id,
                               .player = strike.player,
                               .part = static_cast<std::uint8_t>(strike.part),
                               .wound = static_cast<std::uint8_t>(clean ? 0 : 1),
                               .position = strike.position}});
    Hit("npc.wound");
  }
  Free(*npc, out);
  Fall(*npc, true, strike.player, nowUs, out);
}

void HostWorld::OnEffect(const p::NpcEffect &effect, std::uint64_t nowUs, Outbox &out) {
  auto *npc = Find(effect.npc);
  if (!npc || !npc->Alive())
    return;
  for (const auto record : effect.effects) {
    if (record == kUnconscious) {
      Free(*npc, out);
      Fall(*npc, false, effect.player, nowUs, out);
      return;
    }
    const bool fresh = !npc->effects.contains(record);
    npc->effects[record] = {effect.player, nowUs + kEffectUs};
    if (fresh)
      out.push_back({p::NpcStatus{
          .npc = npc->id, .player = effect.player, .effect = record, .applied = true}});
    if ((record == kStun || record == kEmp) && npc->mode != NpcMode::Held) {
      npc->mode = NpcMode::Staggered;
      npc->modeUntilUs = nowUs + 3'000'000;
    }
    Hit("npc.status-effect");
  }
}

void HostWorld::OnHack(const p::NpcHack &hack, std::uint64_t nowUs, Outbox &out) {
  auto *npc = Find(hack.npc);
  p::NpcHackState state{.npc = hack.npc,
                        .player = hack.player,
                        .action = hack.action,
                        .step = p::HackStep::Started,
                        .uploadMs = hack.uploadMs};
  if (!npc || !npc->Alive()) {
    // Nothing to upload to: its game hears it failed.
    out.push_back({state});
    state.step = p::HackStep::Failed;
    out.push_back({state});
    Hit("npc.upload-failed");
    return;
  }
  out.push_back({state});
  // One upload at a time on a target: a second one waits for the first.
  std::uint64_t start = nowUs;
  for (const auto &upload : m_uploads)
    if (upload.state.npc == hack.npc)
      start = std::max(start, upload.doneUs);
  m_uploads.push_back(
      {state, hack.flags, start + std::uint64_t{std::min(hack.uploadMs, 30'000u)} * 1000});
}

// The refusals of the real host (scripts/tandem/Takedowns.reds OnRequest),
// in its order.
void HostWorld::OnAction(const p::NpcAction &action, std::uint64_t nowUs, const Scene &scene,
                         const SeenPlayer &self, Outbox &out) {
  using Kind = p::NpcActionKind;
  auto *npc = Find(action.npc);
  const auto *asker = action.player == self.id ? &self : scene.Find(action.player);
  const auto kind = action.kind;
  const bool ending = kind == Kind::Takedown || kind == Kind::Knockout || kind == Kind::Animate ||
                      kind == Kind::Finisher;
  std::uint8_t reason = 0;
  if (!npc || !asker) {
    reason = p::kRefusedGone;
  } else if (npc->holder != p::kNoPlayer && npc->holder != action.player && kind != Kind::Release) {
    reason = p::kRefusedHeld;
  } else if (kind == Kind::Grab && npc->body) {
    reason = p::kRefusedGone;
  } else if (ending && npc->mode == NpcMode::Dead) {
    reason = p::kRefusedGone;
  } else if ((kind == Kind::Grab || kind == Kind::PickUp) && asker->heard &&
             Distance(npc->position, asker->position) > 6.f) {
    reason = p::kRefusedTooFar;
  } else if (kind == Kind::Finisher && npc->poise > 0.f &&
             npc->health / npc->spec.health > action.amount + 0.05f) {
    reason = p::kRefusedConditions;
  } else if (kind == Kind::PickUp && !npc->body && npc->holder == p::kNoPlayer) {
    reason = p::kRefusedConditions; // only a body, or one we hold, is carried
  } else if ((kind == Kind::Drop || kind == Kind::Throw) && npc->holder != action.player) {
    reason = p::kRefusedNotYours;
  }
  out.push_back({p::NpcActionReply{
                     .npc = action.npc, .kind = kind, .accepted = reason == 0, .reason = reason},
                 action.player});
  if (reason != 0) {
    constexpr const char *why[] = {"",
                                   "npc.refuse-gone",
                                   "npc.refuse-held",
                                   "npc.refuse-conditions",
                                   "npc.refuse-not-yours",
                                   "npc.refuse-too-far"};
    Hit(why[reason]);
    return;
  }
  const auto hold = [&](p::NpcHoldKind how) {
    npc->holder = action.player;
    npc->hold = how;
    npc->heldSinceUs = nowUs;
    if (npc->Alive())
      npc->mode = NpcMode::Held;
    out.push_back(
        {p::NpcHold{.npc = npc->id, .player = action.player, .how = how, .name = action.name}});
  };
  switch (kind) {
  case Kind::Grab:
    hold(p::NpcHoldKind::Grappled);
    Hit("npc.grab");
    break;
  case Kind::Release:
    if (npc->holder == action.player) {
      Free(*npc, out);
      if (npc->Alive()) {
        npc->mode = NpcMode::Idle;
        Detect(*npc, action.player, nowUs, out);
      }
      Hit("npc.release");
    }
    break;
  case Kind::Takedown:
  case Kind::Knockout:
  case Kind::Animate:
    hold(p::NpcHoldKind::Takedown);
    m_endings.push_back({.npc = npc->id,
                         .atUs = nowUs + m_config.takedownUs,
                         .dead = kind == Kind::Takedown,
                         .by = kind == Kind::Animate ? p::kNoPlayer : action.player});
    Hit(kind == Kind::Knockout ? "npc.takedown-knockout" : "npc.takedown");
    break;
  case Kind::Finisher:
    hold(p::NpcHoldKind::Finisher);
    m_endings.push_back({.npc = npc->id,
                         .atUs = nowUs + m_config.takedownUs,
                         .dead = true,
                         .by = action.player,
                         .part = static_cast<std::int8_t>(action.item ? 0 : -1)});
    Hit("npc.finisher");
    break;
  case Kind::PickUp:
    hold(p::NpcHoldKind::Carried);
    Hit("npc.carry");
    break;
  case Kind::Drop:
  case Kind::Throw:
    Free(*npc, out);
    npc->position = asker->position + action.direction * (kind == Kind::Throw ? 0.4f : 0.f);
    npc->goneAtUs = nowUs; // the body lies anew
    if (npc->Alive())
      npc->mode = NpcMode::Alerted;
    Hit(kind == Kind::Throw ? "npc.throw" : "npc.drop");
    break;
  case Kind::Dispose: {
    Free(*npc, out);
    npc->killer = action.player;
    out.push_back(
        {p::NpcGone{.npc = npc->id, .reason = p::kGoneDisposed, .killer = action.player}});
    m_npcs.erase(npc->id);
    Hit("npc.dispose");
    break;
  }
  }
}

void HostWorld::OnNoise(const p::Noise &noise, std::uint64_t nowUs, const Scene &scene,
                        const SeenPlayer &self) {
  const auto *source = noise.player == self.id ? &self : scene.Find(noise.player);
  if (!source || !source->heard)
    return;
  const float radius = noise.radius > 0.f ? noise.radius : 25.f;
  for (auto &[id, npc] : m_npcs) {
    if (!npc.Alive() || npc.mode == NpcMode::Held || Fights(npc) ||
        Distance(npc.position, source->position) > radius)
      continue;
    npc.awareness[noise.player] = std::max(npc.awareness[noise.player], 0.6f);
    if (npc.mode == NpcMode::Idle || npc.mode == NpcMode::Patrol) {
      npc.mode = NpcMode::Alerted;
      npc.modeUntilUs = nowUs + 6'000'000;
      npc.goal = source->position;
    }
    Hit("npc.hear");
  }
}

void HostWorld::OnShotHit(const p::ShotHit &) { Hit("npc.shot-hit-reported"); }

void HostWorld::OnPlayerLeft(p::PlayerId player, std::uint64_t nowUs, Outbox &out) {
  for (auto &[id, npc] : m_npcs) {
    npc.awareness.erase(player);
    npc.detected &= static_cast<std::uint8_t>(~Bit(player));
    if (npc.target == player)
      npc.target = p::kNoPlayer;
    if (npc.holder != player)
      continue;
    // The endpoint told everyone it is free; ours is free too.
    npc.holder = p::kNoPlayer;
    npc.hold = p::NpcHoldKind::None;
    if (npc.Alive()) {
      npc.mode = NpcMode::Alerted;
      npc.modeUntilUs = nowUs + 4'000'000;
    }
    Hit("npc.freed-on-leave");
  }
  std::erase_if(m_endings, [&](const Ending &ending) { return ending.by == player; });
  (void)out;
}

void HostWorld::Rebuild(std::uint64_t nowUs, Outbox &out) {
  for (auto &[id, npc] : m_npcs) {
    Free(npc, out);
    if (npc.Alive())
      Remove(npc, p::kGoneAway, out);
    else
      out.push_back({p::NpcGone{.npc = id, .reason = p::kGoneAway}});
  }
  m_npcs.clear();
  m_uploads.clear();
  m_endings.clear();
  m_bubbles.Clear();
  // The place we came to has its own people, once it has streamed in.
  m_waveAtUs = nowUs + 2'000'000;
  m_nextStatesUs = nowUs + 500'000;
  Hit("world.rebuilt");
}

std::string HostWorld::Check(const Endpoint &endpoint) const {
  const auto &shared = endpoint.SharedNpcs();
  for (const auto &[id, npc] : m_npcs) {
    if (npc.Alive() != shared.contains(id))
      return std::format("npc {} is {} in the world and {} in the session's record", id,
                         NameOf(npc.mode), shared.contains(id) ? "shared" : "not shared");
    if (!npc.Alive())
      continue; // a body's hold ends with its NPC in the record
    const auto held = endpoint.NpcHolds().find(id);
    const auto holder = held == endpoint.NpcHolds().end() ? p::kNoPlayer : held->second.player;
    if (holder != npc.holder)
      return std::format("npc {} is held by player {} in the world and by player {} in the "
                         "session's record",
                         id, npc.holder, holder);
  }
  for (const auto &[id, spawn] : shared)
    if (!m_npcs.contains(id))
      return std::format("npc {} is in the session's record and not in the world", id);
  return {};
}
} // namespace tandem::sim
