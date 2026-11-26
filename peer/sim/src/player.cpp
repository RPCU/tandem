#include "tandem/sim/player.hpp"

#include "tandem/movement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace tandem::sim {
namespace {
namespace p = proto;

constexpr std::uint64_t kSecond = 1'000'000;
constexpr std::uint64_t kNever = std::numeric_limits<std::uint64_t>::max() / 2;
constexpr std::uint8_t kStimGunshot = 28; // gamedataStimType, as tandem-peer --shoot
constexpr std::uint8_t kAttackRanged = 10, kAttackMelee = 7, kAttackExplosion = 3;

struct Hack {
  const char *action, *effect;
};
constexpr Hack kHacks[] = {{"QuickHack.BlindHack", "BaseStatusEffect.Blind"},
                           {"QuickHack.EMPOverloadHack", "BaseStatusEffect.EMP"},
                           {"QuickHack.SystemCollapseHack", "BaseStatusEffect.Unconscious"},
                           {"QuickHack.ShortCircuitHack", "BaseStatusEffect.Stun"}};

std::uint8_t Bit(p::PlayerId player) {
  return player >= 1 && player <= 8 ? static_cast<std::uint8_t>(1u << (player - 1)) : 0;
}
} // namespace

SimV::SimV(const Persona &persona, Config config, Random random, Coverage *coverage, Motion *motion)
    : m_persona(persona), m_config(std::move(config)), m_random(random), m_coverage(coverage),
      m_motion(motion) {
  m_body.position = m_goalAt = m_config.start;
  m_self.heard = true;
  m_self.position = m_body.position;
  if (m_coverage) {
    for (std::size_t goal = 0; goal < static_cast<std::size_t>(Goal::Count); ++goal)
      if (m_persona.weights[goal] > 0.f)
        m_coverage->Declare(std::string("v.") + NameOf(static_cast<Goal>(goal)));
    for (const char *feature : {"v.hit-by-npc",
                                "v.shot-at",
                                "v.shot-hit",
                                "v.hacked",
                                "v.area-hit",
                                "v.down",
                                "v.out",
                                "v.respawn",
                                "v.revived",
                                "v.revive-other",
                                "v.being-revived",
                                "v.answer-call",
                                "v.suggest",
                                "v.regroup-yes",
                                "v.regroup-no",
                                "v.take-item",
                                "v.item-taken",
                                "v.item-lost",
                                "v.refused",
                                "v.finisher",
                                "v.carry",
                                "v.dispose",
                                "v.hack-landed",
                                "v.car-hacked",
                                "v.epoch",
                                "v.strike",
                                "v.melee",
                                "v.headshot",
                                "v.grab",
                                "v.takedown-lethal",
                                "v.takedown-knockout"})
      m_coverage->Declare(feature);
    if (m_persona.worstMoments)
      for (const char *feature : {"v.chaos-strike-at-rebuild", "v.chaos-grab-dead",
                                  "v.chaos-grab-held", "v.chaos-early-release"})
        m_coverage->Declare(feature);
  }
}

std::uint64_t SimV::Seconds(float low, float high) {
  const float scale = std::max(m_persona.tempo * m_config.pace, 0.05f);
  return static_cast<std::uint64_t>(m_random.Between(low, high) / scale * 1e6f);
}

void SimV::Introduce(Outbox &out) const {
  // A look of its own (no clothes needed: the puppet's body is enough here).
  out.push_back({p::Appearance{.body = m_config.body}});
  out.push_back({p::Held{.right = m_held}});
  if (m_self.life != p::Life::Up)
    out.push_back({p::Vitals{.life = m_self.life}});
}

void SimV::Rejoined(std::uint64_t nowUs) {
  m_self.life = p::Life::Up;
  m_self.seat = {};
  m_self.abilities = 0;
  m_self.timeScale = 1.f;
  m_self.stealth = 0;
  m_self.visibility = 1.f;
  m_self.place = p::Place::City;
  m_self.transit = p::TransitKind::None;
  m_health = 100.f;
  m_held = 0;
  m_car.reset();
  m_crouch = m_paused = false;
  m_reviving.reset();
  m_claimed.clear();
  m_seenItems.clear();
  m_brokenGuard.clear();
  m_lastCall = 0;
  m_lastHub = -1;
  m_answerUs = m_suggestUs = 0;
  m_aloneDownUs = 0;
  m_task = {.goal = Goal::Follow, .startUs = nowUs, .untilUs = nowUs + Seconds(2.f, 4.f)};
  m_goal = Goal::Follow;
}

void SimV::Hold(std::uint64_t right, Outbox &out) {
  if (right == m_held)
    return;
  m_held = right;
  out.push_back({p::Held{.right = right}});
}

void SimV::SetLife(p::Life life, std::uint64_t nowUs, Outbox &out) {
  if (m_self.life == life)
    return;
  m_self.life = life;
  m_lifeUs = nowUs;
  m_aloneDownUs = 0;
  out.push_back({p::Vitals{.life = life}});
  if (life != p::Life::Up) {
    // Whatever it was doing stops: it lies there.
    Finish(nowUs, out);
    m_reviving.reset();
    m_goal = Goal::GoDown;
    m_task = {.goal = Goal::GoDown, .startUs = nowUs, .untilUs = kNever};
    Hit(life == p::Life::Down ? "v.down" : "v.out");
  }
}

void SimV::Hurt(float damage, bool cannotKill, std::uint64_t nowUs, const Scene &scene,
                Outbox &out) {
  if (m_self.life != p::Life::Up || m_self.place != p::Place::City)
    return;
  m_health -= damage;
  m_hurtUs = nowUs;
  if (m_health > 0.f)
    return;
  if (cannotKill) {
    m_health = 1.f;
    return;
  }
  m_health = 0.f;
  SetLife(scene.AnyoneElseUp() ? p::Life::Down : p::Life::Out, nowUs, out);
}

void SimV::Travel(const p::Vec3 &to, std::uint8_t reason, std::uint64_t nowUs, Outbox &out) {
  if (m_self.seat.vehicle != 0) {
    out.push_back({p::Seat{}});
    m_self.seat = {};
    m_car.reset();
  }
  m_motion->Teleport(m_body, to);
  m_goalAt = to;
  out.push_back({p::Epoch{.reason = reason}});
  m_rebuilt = true;
  Hit("v.epoch");
  (void)nowUs;
}

const SeenNpc *SimV::Target(const Scene &scene, bool forTakedown) const {
  const SeenNpc *best = nullptr;
  float bestDistance = forTakedown ? 40.f : 55.f;
  for (const auto &[id, npc] : scene.npcs) {
    if (!npc.live || npc.body || npc.holder != p::kNoPlayer)
      continue;
    if (forTakedown && (npc.detected & Bit(m_self.id)))
      continue; // it sees us coming
    if (!forTakedown && !m_persona.hostile && npc.detected == 0 && m_health >= 100.f)
      continue; // not in a fight: left alone
    const float distance = Distance(npc.position, m_body.position);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = &npc;
    }
  }
  return best;
}

bool SimV::Possible(Goal goal, const Scene &scene) const {
  const auto *partner = scene.Partner();
  const bool near =
      partner && partner->Present() && Distance(partner->position, m_body.position) < 30.f;
  const bool seated = m_self.seat.vehicle != 0;
  switch (goal) {
  case Goal::Follow:
    return partner && partner->Present();
  case Goal::Fight:
  case Goal::Hack:
    return Target(scene, false) != nullptr;
  case Goal::Takedown:
    return Target(scene, true) != nullptr;
  case Goal::Drive:
    return !seated;
  case Goal::Ride:
    return !seated && partner && partner->Driving() && partner->seat.vehicle != 0 &&
           Distance(partner->position, m_body.position) < 25.f;
  case Goal::Regroup:
    return partner != nullptr;
  case Goal::DropItem:
  case Goal::Grenade:
    return near;
  case Goal::LootQuest:
    return m_seeds < 16;
  case Goal::GoDown:
    return scene.AnyoneElseUp() || m_persona.worstMoments;
  case Goal::CarHack:
    return partner && partner->Driving() && partner->seat.vehicle != 0;
  case Goal::Dial:
    return !m_config.host;
  case Goal::Elsewhere:
    return !m_config.host; // the host's other worlds are its story's
  default:
    return true;
  }
}

void SimV::Pick(std::uint64_t nowUs, const Scene &scene, Outbox &out) {
  std::array<float, static_cast<std::size_t>(Goal::Count)> weights{};
  float total = 0.f;
  for (std::size_t i = 0; i < weights.size(); ++i) {
    const auto goal = static_cast<Goal>(i);
    weights[i] = Possible(goal, scene) ? m_persona.Weight(goal) : 0.f;
    total += weights[i];
  }
  // A loner goes to its own region first.
  if (m_persona.region > 0.f && !m_rebuilt && m_task.startUs == 0) {
    Begin(Goal::FastTravel, nowUs, scene, out);
    return;
  }
  if (total <= 0.f) {
    Begin(Goal::Wander, nowUs, scene, out);
    return;
  }
  float pick = m_random.Between(0.f, total);
  for (std::size_t i = 0; i < weights.size(); ++i) {
    pick -= weights[i];
    if (weights[i] > 0.f && pick <= 0.f) {
      Begin(static_cast<Goal>(i), nowUs, scene, out);
      return;
    }
  }
  Begin(Goal::Wander, nowUs, scene, out);
}

void SimV::Begin(Goal goal, std::uint64_t nowUs, const Scene &scene, Outbox &out) {
  const auto *partner = scene.Partner();
  m_goal = goal;
  m_task = {.goal = goal, .startUs = nowUs, .nextUs = nowUs};
  m_gait = Gait::Walk;
  m_crouch = false;
  Hit(std::string("v.") + NameOf(goal));
  const auto lasts = [&](float low, float high) { m_task.untilUs = nowUs + Seconds(low, high); };
  const auto front = m_body.position + Forward(m_body.yaw) * 1.5f;
  switch (goal) {
  case Goal::Follow:
    lasts(8.f, 20.f);
    break;
  case Goal::Wander:
    lasts(8.f, 20.f);
    m_task.step = -1; // a point to go to comes first
    break;
  case Goal::Fight: {
    lasts(10.f, 25.f);
    const auto *npc = Target(scene, false);
    m_task.npc = npc ? npc->id : 0;
    m_task.melee = m_random.Chance(0.35);
    Hold(TweakDBID(m_task.melee ? m_config.blade : m_config.gun), out);
    m_gait = Gait::Run;
    break;
  }
  case Goal::Hack: {
    lasts(5.f, 9.f);
    const auto *npc = Target(scene, false);
    m_task.npc = npc ? npc->id : 0;
    m_task.step = static_cast<int>(m_random.Index(std::size(kHacks)));
    out.push_back(
        {p::NpcHack{.npc = m_task.npc,
                    .action = TweakDBID(kHacks[m_task.step].action),
                    .uploadMs = 1500,
                    .flags = m_random.Chance(0.5) ? p::kUploadTraceable : std::uint8_t{0}}});
    break;
  }
  case Goal::Takedown: {
    lasts(15.f, 25.f);
    const auto *npc = Target(scene, true);
    m_task.npc = npc ? npc->id : 0;
    m_crouch = true;
    m_self.stealth = p::kStealthCrouch;
    out.push_back({p::Stealth{.flags = p::kStealthCrouch, .visibility = 1.f}});
    break;
  }
  case Goal::Sandevistan:
    lasts(4.f, 6.f);
    m_self.abilities = p::kTimeSandevistan;
    m_self.timeScale = 0.25f;
    out.push_back({p::TimeEffect{.abilities = p::kTimeSandevistan, .scale = 0.25f}});
    break;
  case Goal::Drive:
    lasts(20.f, 40.f);
    m_car = m_body;
    m_carSpeed = 0.f;
    m_self.seat = {
        .vehicle = TweakDBID(m_config.car), .seat = CName("seat_front_left"), .owner = m_self.id};
    out.push_back({m_self.seat});
    if (m_random.Chance(0.5))
      out.push_back(
          {p::VehicleRadio{.on = true, .station = static_cast<std::int8_t>(m_random.Index(14))}});
    m_task.step = -1;
    break;
  case Goal::Ride:
    lasts(15.f, 30.f);
    m_task.who = partner->id;
    m_self.seat = {
        .vehicle = partner->seat.vehicle, .seat = CName("seat_front_right"), .owner = partner->id};
    out.push_back({m_self.seat});
    break;
  case Goal::FastTravel: {
    lasts(6.f, 10.f);
    // Somewhere far: a loner's own region, else across the map.
    const float angle = m_random.Between(0.f, 2.f * kPi);
    const float range = m_persona.region > 0.f ? m_persona.region : m_random.Between(300.f, 800.f);
    const p::Vec3 anchor = m_persona.region > 0.f ? m_config.start : m_body.position;
    Travel(anchor + p::Vec3{std::cos(angle) * range, std::sin(angle) * range, 0.f},
           p::kEpochFastTravel, nowUs, out);
    m_task.where = m_body.position;
    m_task.step = 0;
    break;
  }
  case Goal::Regroup:
    lasts(2.f, 3.f);
    out.push_back({p::Regroup{.offer = ++m_offers,
                              .reason = p::RegroupReason::Asked,
                              .position = m_body.position,
                              .yaw = m_body.yaw}});
    break;
  case Goal::Metro:
    lasts(16.f, 24.f);
    m_task.where = Forward(m_random.Between(0.f, 2.f * kPi));
    m_self.transit = p::TransitKind::Metro;
    out.push_back({p::Transit{.kind = p::TransitKind::Metro, .at = 3, .next = 4}});
    break;
  case Goal::Elsewhere: {
    lasts(8.f, 14.f);
    const auto place = m_random.Chance(0.5) ? p::Place::Braindance : p::Place::Cyberspace;
    m_self.place = place;
    out.push_back({p::Elsewhere{.place = place,
                                .flags = p::kElsewhereMoved | p::kElsewhereSettled,
                                .anchor = m_body.position,
                                .yaw = m_body.yaw}});
    break;
  }
  case Goal::Pause:
    lasts(2.f, 4.f);
    m_paused = m_self.paused = true;
    out.push_back({p::Pause{.paused = true}});
    break;
  case Goal::Camo:
    lasts(4.f, 6.f);
    m_self.stealth = p::kStealthCamo;
    m_self.visibility = 0.1f;
    out.push_back({p::Stealth{.flags = p::kStealthCamo,
                              .visibility = 0.1f,
                              .camo = TweakDBID("BaseStatusEffect.OpticalCamoPlayerBuff")}});
    break;
  case Goal::DropItem:
    lasts(2.f, 3.f);
    out.push_back({p::WorldItem{.token = ++m_tokens,
                                .position = front,
                                .stacks = {{TweakDBID("Items.FirstAidWhiffV0"), 0, 1}}}});
    break;
  case Goal::LootQuest:
    lasts(1.f, 2.f);
    ++m_seeds;
    out.push_back({p::SharedItem{.kind = p::kSharedLooted,
                                 .item = {.record = TweakDBID("Items.q003_chip"),
                                          .seed = static_cast<std::uint32_t>(m_random.Next() | 1u),
                                          .quantity = 1}}});
    break;
  case Goal::GoDown:
    SetLife(scene.AnyoneElseUp() ? p::Life::Down : p::Life::Out, nowUs, out);
    break;
  case Goal::CarHack:
    lasts(2.f, 3.f);
    out.push_back(
        {p::VehicleHack{.driver = partner->id, .hack = p::CarHack::ForceBrakes, .seconds = 3.f}});
    break;
  case Goal::Grenade:
    lasts(1.f, 2.f);
    out.push_back({p::AreaHit{.player = partner->id,
                              .physical = 20.f,
                              .attackType = kAttackExplosion,
                              .flags = p::kHitCannotKill,
                              .position = partner->position,
                              .direction = Forward(YawTo(m_body.position, partner->position)),
                              .weapon = TweakDBID("Items.GrenadeFragRegular")}});
    break;
  case Goal::Dial:
    lasts(1.f, 2.f);
    out.push_back({p::PhoneAction{.action = p::kPhoneDial, .contact = CName("judy")}});
    break;
  case Goal::Count:
    break;
  }
}

// The goal ends: what it started is put back.
void SimV::Finish(std::uint64_t nowUs, Outbox &out) {
  switch (m_task.goal) {
  case Goal::Fight:
    Hold(0, out);
    break;
  case Goal::Takedown:
    // Still holding it (a grab that never ended, or a body on the shoulder).
    if (m_task.accepted && m_task.step >= 1 && m_task.step != 3)
      out.push_back({p::NpcAction{.npc = m_task.npc,
                                  .kind = m_task.step >= 4 ? p::NpcActionKind::Drop
                                                           : p::NpcActionKind::Release}});
    if (m_self.stealth & p::kStealthCrouch) {
      m_self.stealth = 0;
      out.push_back({p::Stealth{.visibility = 1.f}});
    }
    break;
  case Goal::Sandevistan:
    m_self.abilities = 0;
    m_self.timeScale = 1.f;
    out.push_back({p::TimeEffect{}});
    break;
  case Goal::Drive:
  case Goal::Ride:
    if (m_self.seat.vehicle != 0) {
      out.push_back({p::Seat{}});
      m_self.seat = {};
      m_body.position = m_body.position + Forward(m_body.yaw + kPi / 2.f) * 2.f;
      m_motion->Teleport(m_body, m_body.position);
    }
    m_car.reset();
    break;
  case Goal::Metro:
    m_self.transit = p::TransitKind::None;
    out.push_back({p::Transit{}});
    m_body.position.z = m_config.start.z;
    m_motion->Teleport(m_body, m_body.position);
    break;
  case Goal::Elsewhere:
    m_self.place = p::Place::City;
    out.push_back({p::Elsewhere{.place = p::Place::City}});
    break;
  case Goal::Pause:
    m_paused = m_self.paused = false;
    out.push_back({p::Pause{.paused = false}});
    break;
  case Goal::Camo:
    m_self.stealth = 0;
    m_self.visibility = 1.f;
    out.push_back({p::Stealth{.visibility = 1.f}});
    break;
  default:
    break;
  }
  m_crouch = false;
  m_task.goal = Goal::Wander;
  m_task.untilUs = nowUs;
}

void SimV::Work(std::uint64_t nowUs, const Scene &scene, Outbox &out) {
  auto &task = m_task;
  const auto *partner = scene.Partner();
  const auto npcIt = scene.npcs.find(task.npc);
  const SeenNpc *npc = npcIt == scene.npcs.end() ? nullptr : &npcIt->second;
  const auto end = [&] { task.untilUs = nowUs; };
  switch (task.goal) {
  case Goal::Follow: {
    if (!partner || !partner->Present()) {
      end();
      return;
    }
    const float distance = Distance(partner->position, m_body.position);
    m_gait = distance > 25.f ? Gait::Sprint : distance > 9.f ? Gait::Run : Gait::Walk;
    // Beside it, at its persona's distance (down: right by it, to revive it).
    const float keep = partner->life == p::Life::Down ? 0.5f : m_persona.keep;
    m_goalAt = partner->position + Forward(partner->yaw + kPi / 2.f) * keep;
    break;
  }
  case Goal::Wander:
    if (task.step < 0 || Distance(m_body.position, m_goalAt) < 1.5f) {
      task.step = 0;
      const p::Vec3 home = m_persona.region <= 0.f && partner && partner->Present()
                               ? partner->position
                               : m_body.position;
      const float angle = m_random.Between(0.f, 2.f * kPi);
      m_goalAt =
          home + p::Vec3{std::cos(angle), std::sin(angle), 0.f} * m_random.Between(5.f, 25.f);
      m_gait = m_random.Chance(0.2) ? Gait::Run : Gait::Walk;
    }
    break;
  case Goal::Fight: {
    if (!npc || !npc->live || npc->body) {
      const auto *next = Target(scene, false);
      if (!next) {
        end();
        return;
      }
      task.npc = next->id;
      return;
    }
    const float distance = Distance(npc->position, m_body.position);
    const float reach = task.melee ? 1.6f : 14.f;
    if (distance > reach) {
      m_goalAt = npc->position;
      m_gait = distance > 25.f ? Gait::Sprint : Gait::Run;
      return;
    }
    m_goalAt = m_body.position;
    m_body.yaw = YawTo(m_body.position, npc->position);
    if (nowUs < task.nextUs)
      return;
    task.nextUs = nowUs + (task.melee ? Seconds(0.8f, 1.2f) : Seconds(0.25f, 0.4f));
    ++task.step;
    out.push_back({p::Shot{.effect = static_cast<std::uint8_t>(task.melee ? 200 : 16)}});
    if (!task.melee && task.step % 3 == 1)
      out.push_back({p::Noise{.stim = kStimGunshot, .radius = 25.f}});
    // A finisher once its guard broke and it is weak (melee).
    if (task.melee && m_brokenGuard.contains(npc->id) && npc->health < 0.5f && !task.asked) {
      task.asked = true;
      out.push_back({p::NpcAction{.npc = npc->id,
                                  .kind = p::NpcActionKind::Finisher,
                                  .name = CName("playFinisher"),
                                  .item = TweakDBID(m_config.blade),
                                  .amount = npc->health}});
      Hit("v.finisher");
      return;
    }
    if (!task.melee && task.step % 2 == 0)
      return; // not every bullet lands
    p::NpcStrike strike{.npc = npc->id,
                        .damage = task.melee ? m_random.Between(20.f, 35.f)
                                             : m_random.Between(12.f, 28.f),
                        .poise = task.melee ? 30.f : 4.f,
                        .attackType = task.melee ? kAttackMelee : kAttackRanged,
                        .position = npc->position + p::Vec3{0.f, 0.f, 1.2f},
                        .direction = Forward(m_body.yaw)};
    if (task.melee) {
      strike.flags |= p::kStrikeMelee;
      strike.part = static_cast<std::int8_t>(m_random.Index(6));
      if (m_random.Chance(0.15))
        strike.flags |= p::kStrikeNonlethal;
      Hit("v.melee");
    } else if (m_random.Chance(0.15)) {
      strike.flags |= p::kStrikeHeadshot | p::kStrikeWeakSpot;
      strike.damage *= 2.f;
      strike.part = 0;
      Hit("v.headshot");
    }
    out.push_back({strike});
    Hit("v.strike");
    break;
  }
  case Goal::Hack:
    break; // its upload's end (Heard) lands the effect
  case Goal::Takedown: {
    if (task.step == 0) {
      if (!npc || !npc->live || npc->body || npc->holder != p::kNoPlayer) {
        end();
        return;
      }
      m_goalAt = npc->position;
      m_gait = Gait::Walk;
      if (Distance(npc->position, m_body.position) > 1.4f)
        return;
      task.step = 1;
      task.asked = true;
      out.push_back({p::NpcAction{
          .npc = npc->id, .kind = p::NpcActionKind::Grab, .name = CName("GrappleTarget")}});
      Hit("v.grab");
    } else if (task.step == 2 && nowUs >= task.nextUs) {
      // Chaos lets go at once, now and then.
      if (m_persona.worstMoments && m_random.Chance(0.25)) {
        out.push_back({p::NpcAction{
            .npc = task.npc, .kind = p::NpcActionKind::Release, .name = CName("BreakFree")}});
        task.accepted = false;
        Hit("v.chaos-early-release");
        end();
        return;
      }
      const bool lethal = m_random.Chance(0.5);
      task.step = 3;
      task.asked = true;
      task.nextUs = nowUs + Seconds(3.f, 4.f);
      out.push_back(
          {p::NpcAction{.npc = task.npc,
                        .kind = lethal ? p::NpcActionKind::Takedown : p::NpcActionKind::Knockout,
                        .name = CName(lethal ? "kill" : "setUnconscious")}});
      Hit(lethal ? "v.takedown-lethal" : "v.takedown-knockout");
    } else if (task.step == 3 && npc && npc->body && npc->holder == p::kNoPlayer) {
      // The body: carried off now and then.
      if (!m_random.Chance(0.6)) {
        end();
        return;
      }
      task.step = 4;
      task.asked = true;
      task.accepted = false;
      out.push_back({p::NpcAction{.npc = task.npc, .kind = p::NpcActionKind::PickUp}});
    } else if (task.step == 3 && nowUs >= task.nextUs) {
      end(); // the takedown never brought a body
    } else if (task.step == 5) {
      m_gait = Gait::Walk;
      if (nowUs < task.nextUs)
        return;
      const auto kind = m_random.Chance(0.2)   ? p::NpcActionKind::Dispose
                        : m_random.Chance(0.5) ? p::NpcActionKind::Throw
                                               : p::NpcActionKind::Drop;
      out.push_back({p::NpcAction{.npc = task.npc,
                                  .kind = kind,
                                  .name = kind == p::NpcActionKind::Dispose ? CName("kill") : 0,
                                  .direction = Forward(m_body.yaw) * 3.f}});
      if (kind == p::NpcActionKind::Dispose)
        Hit("v.dispose");
      task.accepted = false;
      task.step = 6;
      task.untilUs = nowUs + Seconds(1.f, 2.f);
    }
    break;
  }
  case Goal::Drive: {
    if (task.step < 0 || Distance(m_car->position, m_goalAt) < 6.f) {
      task.step = 0;
      const float angle = m_random.Between(0.f, 2.f * kPi);
      m_goalAt = m_car->position +
                 p::Vec3{std::cos(angle), std::sin(angle), 0.f} * m_random.Between(60.f, 140.f);
    }
    break;
  }
  case Goal::Ride: {
    const auto *driver = scene.Find(task.who);
    if (!driver || driver->seat.vehicle == 0 || !driver->Driving())
      end();
    break;
  }
  case Goal::FastTravel:
    if (task.step == 0 && nowUs >= task.startUs + Seconds(1.f, 2.f)) {
      task.step = 1;
      out.push_back({p::Regroup{.offer = ++m_offers,
                                .reason = p::RegroupReason::FastTravel,
                                .point = TweakDBID("FastTravelPoints.wat_kab_ft_01"),
                                .position = m_body.position,
                                .yaw = m_body.yaw}});
    }
    if (Distance(m_body.position, m_goalAt) < 1.5f) {
      const float angle = m_random.Between(0.f, 2.f * kPi);
      m_goalAt = task.where + p::Vec3{std::cos(angle), std::sin(angle), 0.f} * 10.f;
    }
    break;
  case Goal::Metro: {
    // Along a line, 8 m up, at 20 m/s; the ride skips (ten times faster) for
    // a stretch halfway.
    const float span = static_cast<float>(task.untilUs - task.startUs);
    const float at = static_cast<float>(nowUs - task.startUs) / std::max(span, 1.f);
    const bool skipping = at > 0.4f && at < 0.55f;
    if (skipping != (task.step == 1)) {
      task.step = skipping ? 1 : 2;
      out.push_back({p::Transit{.kind = p::TransitKind::Metro,
                                .flags = skipping ? p::kTransitSkipping : std::uint8_t{0},
                                .at = static_cast<std::uint8_t>(skipping ? 3 : 5),
                                .next = static_cast<std::uint8_t>(skipping ? 5 : 6)}});
    }
    break;
  }
  case Goal::GoDown:
    if (m_self.life == p::Life::Up)
      end();
    break;
  default:
    break;
  }
}

void SimV::Move(float dt, const Scene &scene) {
  if (m_self.life != p::Life::Up) {
    m_body.velocity = {};
    m_body.stance = 0;
    m_body.flags =
        movement::Pack({.known = true, .detailed = movement::Detailed::Knockdown, .felled = true});
  } else if (m_paused || m_self.place != p::Place::City || m_reviving) {
    m_body.velocity = {};
  } else if (m_task.goal == Goal::Metro) {
    const float speed = (m_task.step == 1 ? 200.f : 20.f);
    m_body.velocity = m_task.where * speed;
    m_body.position = m_body.position + m_task.where * (speed * dt);
    m_body.position.z = m_config.start.z + 8.f;
    m_body.yaw = YawTo({}, m_task.where);
    m_body.flags =
        movement::Pack({.known = true, .detailed = movement::Detailed::Stand, .workspot = true});
  } else if (m_car) {
    auto &car = *m_car;
    const bool braked = m_lastUs < m_brakedUntilUs;
    const float target = braked ? 0.f : 12.f;
    m_carSpeed += std::clamp(target - m_carSpeed, -8.f * dt, 4.f * dt);
    const float want = YawTo(car.position, m_goalAt);
    const float turn = std::remainder(want - car.yaw, 2.f * kPi);
    car.yaw += std::clamp(turn, -0.8f * dt, 0.8f * dt);
    car.velocity = Forward(car.yaw) * m_carSpeed;
    car.position = car.position + car.velocity * dt;
    m_body.position = car.position;
    m_body.yaw = car.yaw;
    m_body.velocity = car.velocity;
    m_body.flags =
        movement::Pack({.known = true, .detailed = movement::Detailed::NotInBaseLocomotion});
  } else if (m_self.seat.vehicle != 0) {
    // In the partner's car: carried with it.
    if (const auto *driver = scene.Find(m_self.seat.owner)) {
      const auto &at = driver->car ? driver->car->position : driver->position;
      m_body.position = at;
      m_body.velocity = driver->car ? driver->car->velocity : driver->velocity;
      m_body.yaw = driver->yaw;
    }
    m_body.flags =
        movement::Pack({.known = true, .detailed = movement::Detailed::NotInBaseLocomotion});
  } else {
    m_motion->Step(m_body, m_goalAt, m_gait, m_crouch, dt, m_random,
                   m_task.goal == Goal::Fight || m_task.goal == Goal::Takedown ? 0.3f : 1.f);
  }
  m_self.position = m_body.position;
  m_self.velocity = m_body.velocity;
  m_self.yaw = m_body.yaw;
}

void SimV::Send(std::uint64_t nowUs, Outbox &out) {
  // Away in another world, its game sends where the body is: the anchor.
  out.push_back({p::PlayerState{.position = m_body.position,
                                .orientation = QuatOfYaw(m_body.yaw),
                                .velocity = m_body.velocity,
                                .stance = m_body.stance,
                                .flags = m_body.flags}});
  if (m_car)
    out.push_back({p::VehicleState{.position = m_car->position,
                                   .orientation = QuatOfYaw(m_car->yaw),
                                   .velocity = m_car->velocity}});
  (void)nowUs;
}

void SimV::Tick(std::uint64_t nowUs, const Scene &scene, Outbox &out) {
  if (m_lastUs == 0) {
    m_lastUs = nowUs;
    m_task.untilUs = nowUs + Seconds(1.f, 3.f);
  }
  const float dt = std::min(static_cast<float>(nowUs - m_lastUs) / 1e6f, 0.25f);
  m_lastUs = nowUs;
  m_self.id = scene.self;

  // Life: alone and down for 3 s is out; out for 10 s, up again beside a partner.
  if (m_self.life == p::Life::Down) {
    if (scene.AnyoneElseUp())
      m_aloneDownUs = 0;
    else if (m_aloneDownUs == 0)
      m_aloneDownUs = nowUs;
    else if (nowUs - m_aloneDownUs > 3 * kSecond)
      SetLife(p::Life::Out, nowUs, out);
    if (m_self.life == p::Life::Down && m_persona.givesUpDown > 0.f &&
        nowUs - m_lifeUs > static_cast<std::uint64_t>(m_persona.givesUpDown * 1e6f))
      SetLife(p::Life::Out, nowUs, out);
  } else if (m_self.life == p::Life::Out && nowUs - m_lifeUs > 10 * kSecond) {
    m_health = 100.f;
    SetLife(p::Life::Up, nowUs, out);
    if (const auto *partner = scene.Partner(); partner && partner->Present())
      m_motion->Teleport(m_body, partner->position + p::Vec3{2.f, 0.f, 0.f});
    m_goalAt = m_body.position;
    m_task = {.goal = Goal::Wander, .startUs = nowUs, .untilUs = nowUs + Seconds(1.f, 2.f)};
    Hit("v.respawn");
  }
  if (m_self.life == p::Life::Up && nowUs - m_hurtUs > 5 * kSecond)
    m_health = std::min(100.f, m_health + 4.f * dt);

  // The host's call and hubs (a guest that answers).
  if (!m_config.host && m_persona.answers) {
    if (scene.call && scene.call->phase == 1 && scene.call->answeredBy == p::kNoPlayer) {
      if (scene.call->contact != m_lastCall) {
        m_lastCall = scene.call->contact;
        m_answerUs = nowUs + Seconds(1.f, 3.f);
      }
    } else {
      m_lastCall = 0;
      m_answerUs = 0;
    }
    if (m_answerUs && nowUs >= m_answerUs && m_self.life == p::Life::Up) {
      m_answerUs = 0;
      out.push_back({p::PhoneAction{.action = p::kPhoneAnswer, .contact = m_lastCall}});
      Hit("v.answer-call");
    }
    if (!scene.hub) {
      m_lastHub = -1;
    } else if (scene.hub->hub != m_lastHub) {
      m_lastHub = scene.hub->hub;
      m_suggestUs = nowUs + Seconds(1.f, 2.f);
    }
    if (m_suggestUs && nowUs >= m_suggestUs && scene.hub) {
      m_suggestUs = 0;
      out.push_back({p::ChoiceCursor{
          .hub = scene.hub->hub,
          .index = static_cast<std::int8_t>(m_random.Index(scene.hub->options.size())),
          .kind = p::kCursorSuggest}});
      Hit("v.suggest");
    }
  }

  // What the others drop: a helper picks it up (the host gives it to the first).
  for (const auto &[id, item] : scene.items) {
    if (item.dropper == m_self.id || m_claimed.contains(id) ||
        !(m_persona.helps || m_persona.hostile))
      continue;
    const auto [seen, fresh] = m_seenItems.try_emplace(id, nowUs + Seconds(2.f, 6.f));
    if (nowUs < seen->second || m_self.life != p::Life::Up || m_self.place != p::Place::City)
      continue;
    m_claimed.insert(id);
    for (std::size_t stack = 0; stack < item.stacks.size(); ++stack)
      if (item.stacks[stack].quantity > 0)
        out.push_back({p::WorldItemTaken{.id = id, .stack = static_cast<std::uint8_t>(stack)}});
    Hit("v.take-item");
  }

  // A partner down: a helper goes to it and holds the revive for 10 s.
  if (m_self.life == p::Life::Up && m_persona.helps && !m_paused &&
      m_self.place == p::Place::City) {
    if (m_reviving) {
      const auto *target = scene.Find(m_reviving->first);
      if (!target || target->life != p::Life::Down) {
        if (target)
          out.push_back({p::Revive{.target = m_reviving->first, .step = p::ReviveStep::Cancel}});
        m_reviving.reset();
      } else if (nowUs >= m_reviving->second) {
        out.push_back({p::Revive{.target = m_reviving->first, .step = p::ReviveStep::Done}});
        m_reviving.reset();
      }
    } else {
      for (const auto &[id, player] : scene.players) {
        if (player.life != p::Life::Down || !player.Present())
          continue;
        if (m_self.seat.vehicle != 0 || m_task.goal == Goal::Metro || m_task.goal == Goal::Takedown)
          break; // first out of what it is in
        if (Distance(player.position, m_body.position) > 2.f) {
          // What it was doing waits: it goes to them.
          if (m_task.goal != Goal::Follow || scene.Partner() != &player) {
            Finish(nowUs, out);
            Begin(Goal::Follow, nowUs, scene, out);
          }
          m_goalAt = player.position;
          m_gait = Gait::Run;
          break;
        }
        out.push_back({p::Revive{.target = id, .step = p::ReviveStep::Start}});
        m_reviving = {{id, nowUs + 10 * kSecond}};
        Hit("v.revive-other");
        break;
      }
    }
  }

  if (m_self.life == p::Life::Up && !m_reviving) {
    if (nowUs >= m_task.untilUs) {
      Finish(nowUs, out);
      Pick(nowUs, scene, out);
    } else if (!m_paused) {
      Work(nowUs, scene, out);
    }
  }
  Move(dt, scene);
  if (nowUs >= m_nextStateUs) {
    m_nextStateUs = nowUs + 33'000;
    Send(nowUs, out);
  }
}

void SimV::Heard(const p::Message &message, std::uint64_t nowUs, const Scene &scene, Outbox &out) {
  auto &task = m_task;
  if (const auto *reply = std::get_if<p::NpcActionReply>(&message)) {
    if (task.goal == Goal::Fight && reply->kind == p::NpcActionKind::Finisher) {
      task.asked = false;
      if (reply->accepted)
        m_brokenGuard.erase(reply->npc);
      else
        Hit("v.refused");
      return;
    }
    if (task.goal != Goal::Takedown || reply->npc != task.npc || !task.asked)
      return;
    task.asked = false;
    if (!reply->accepted) {
      task.accepted = false;
      Hit("v.refused");
      task.untilUs = nowUs;
      return;
    }
    task.accepted = true;
    if (reply->kind == p::NpcActionKind::Grab) {
      task.step = 2;
      task.nextUs = nowUs + Seconds(1.f, 2.5f);
    } else if (reply->kind == p::NpcActionKind::PickUp) {
      task.step = 5;
      task.nextUs = nowUs + Seconds(2.f, 4.f);
      const float angle = m_random.Between(0.f, 2.f * kPi);
      m_goalAt = m_body.position + p::Vec3{std::cos(angle), std::sin(angle), 0.f} * 6.f;
      Hit("v.carry");
    }
  } else if (const auto *upload = std::get_if<p::NpcHackState>(&message)) {
    if (task.goal != Goal::Hack || upload->player != m_self.id || upload->npc != task.npc)
      return;
    if (upload->step == p::HackStep::Done) {
      out.push_back(
          {p::NpcEffect{.npc = task.npc, .effects = {TweakDBID(kHacks[task.step].effect)}}});
      Hit("v.hack-landed");
      task.untilUs = nowUs + Seconds(0.5f, 1.f);
    } else if (upload->step == p::HackStep::Failed) {
      task.untilUs = nowUs;
    }
  } else if (const auto *away = std::get_if<p::Elsewhere>(&message)) {
    // The host's story took our V elsewhere, or brought it back.
    m_self.place = away->place;
    m_self.elsewhere = away->flags;
  } else if (const auto *poise = std::get_if<p::NpcPoise>(&message)) {
    if (poise->poise <= 0.f)
      m_brokenGuard.insert(poise->npc);
  } else if (const auto *area = std::get_if<p::AreaHit>(&message)) {
    if (area->player != m_self.id)
      return;
    Hurt(area->physical + area->thermal + area->electric + area->chemical,
         area->flags & p::kHitCannotKill, nowUs, scene, out);
    Hit("v.area-hit");
  }
}

void SimV::HitBy(const p::PlayerHit &hit, std::uint64_t nowUs, const Scene &scene, Outbox &out) {
  Hurt(hit.physical + hit.thermal + hit.electric + hit.chemical, hit.flags & p::kHitCannotKill,
       nowUs, scene, out);
  Hit("v.hit-by-npc");
}

void SimV::On(const SessionEvent &event, std::uint64_t nowUs, const Scene &scene, Outbox &out) {
  std::visit(
      [&](const auto &e) {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<E, events::HitByNpc>) {
          HitBy(e.hit, nowUs, scene, out);
        } else if constexpr (std::is_same_v<E, events::ShotAtUs>) {
          Hit("v.shot-at");
          // Its game fires it from the proxy: some land, by the AI's own odds.
          if (m_self.life != p::Life::Up || !m_self.Present() ||
              m_self.transit != p::TransitKind::None)
            return;
          if (!m_random.Chance(0.35 * std::clamp(e.shot.tbh, 0.f, 1.f)))
            return;
          const auto shooter = scene.npcs.find(e.shot.npc);
          const auto from =
              shooter == scene.npcs.end() ? m_body.position : shooter->second.position;
          out.push_back({p::ShotHit{.npc = e.shot.npc,
                                    .position = m_body.position + p::Vec3{0.f, 0.f, 1.3f},
                                    .direction = Forward(YawTo(from, m_body.position))}});
          Hurt(8.f, false, nowUs, scene, out);
          Hit("v.shot-hit");
        } else if constexpr (std::is_same_v<E, events::HostPlayerHack>) {
          Hit("v.hacked");
          Hurt(5.f, false, nowUs, scene, out);
        } else if constexpr (std::is_same_v<E, events::Combat>) {
          Heard(e.message, nowUs, scene, out);
        } else if constexpr (std::is_same_v<E, events::VehicleHackOn>) {
          if (m_car) {
            m_brakedUntilUs = nowUs + static_cast<std::uint64_t>(e.hack.seconds * 1e6f);
            Hit("v.car-hacked");
          }
        } else if constexpr (std::is_same_v<E, events::ReviveBy>) {
          if (e.revive.step == p::ReviveStep::Start)
            Hit("v.being-revived");
          if (e.revive.step == p::ReviveStep::Done && m_self.life == p::Life::Down) {
            m_health = 40.f;
            SetLife(p::Life::Up, nowUs, out);
            m_task = {.goal = Goal::Wander, .startUs = nowUs, .untilUs = nowUs + Seconds(1.f, 2.f)};
            Hit("v.revived");
          }
        } else if constexpr (std::is_same_v<E, events::SessionLost>) {
          if (m_self.life == p::Life::Down)
            SetLife(p::Life::Out, nowUs, out);
        } else if constexpr (std::is_same_v<E, events::RegroupOffered>) {
          const bool free = m_self.life == p::Life::Up && m_self.seat.vehicle == 0 &&
                            m_self.place == p::Place::City && m_task.goal != Goal::Metro &&
                            m_task.goal != Goal::Takedown && !m_paused;
          if (free && m_persona.comes && m_random.Chance(0.8)) {
            out.push_back(
                {p::RegroupAnswer{.to = e.offer.from, .offer = e.offer.offer, .accepted = true}});
            Finish(nowUs, out);
            Travel(e.offer.position + Forward(e.offer.yaw + kPi / 2.f) * 2.f, p::kEpochFastTravel,
                   nowUs, out);
            m_task = {
                .goal = Goal::Follow, .startUs = nowUs, .untilUs = nowUs + Seconds(6.f, 12.f)};
            Hit("v.regroup-yes");
          } else if (m_random.Chance(0.6)) {
            out.push_back(
                {p::RegroupAnswer{.to = e.offer.from, .offer = e.offer.offer, .accepted = false}});
            Hit("v.regroup-no");
          }
        } else if constexpr (std::is_same_v<E, events::NpcActionAnswered>) {
          Heard(e.reply, nowUs, scene, out);
        } else if constexpr (std::is_same_v<E, events::HostNpcHackState>) {
          Heard(e.state, nowUs, scene, out);
        } else if constexpr (std::is_same_v<E, events::HostNpcCue>) {
          if (e.cue.cue == p::NpcCueKind::PoiseBroken)
            m_brokenGuard.insert(e.cue.npc);
        } else if constexpr (std::is_same_v<E, events::WorldItemTakenBy>) {
          if (!m_claimed.contains(e.taken.id))
            return;
          Hit(e.taken.taker == m_self.id ? "v.item-taken" : "v.item-lost");
        } else if constexpr (std::is_same_v<E, events::EpochChanged>) {
          // Chaos: a hit aimed at the world that was just rebuilt.
          if (!m_persona.worstMoments || m_self.life != p::Life::Up)
            return;
          for (const auto &[id, npc] : scene.npcs)
            if (npc.live) {
              out.push_back({p::NpcStrike{.npc = id, .damage = 10.f, .attackType = kAttackRanged}});
              Hit("v.chaos-strike-at-rebuild");
              return;
            }
        } else if constexpr (std::is_same_v<E, events::HostNpcGone>) {
          // Chaos: a grab on what just died (the host must refuse it).
          if (m_persona.worstMoments && e.gone.reason == p::kGoneDead &&
              m_self.life == p::Life::Up && nowUs >= m_chaosUs && m_task.goal != Goal::Takedown) {
            m_chaosUs = nowUs + 5 * kSecond;
            out.push_back({p::NpcAction{.npc = e.gone.npc, .kind = p::NpcActionKind::Grab}});
            Hit("v.chaos-grab-dead");
          }
        } else if constexpr (std::is_same_v<E, events::HostNpcHold>) {
          // Chaos: a grab on what someone else holds.
          if (m_persona.worstMoments && e.hold.player != p::kNoPlayer &&
              e.hold.player != m_self.id && m_self.life == p::Life::Up && nowUs >= m_chaosUs &&
              m_task.goal != Goal::Takedown) {
            m_chaosUs = nowUs + 5 * kSecond;
            out.push_back({p::NpcAction{.npc = e.hold.npc, .kind = p::NpcActionKind::Grab}});
            Hit("v.chaos-grab-held");
          }
        }
      },
      event);
}
} // namespace tandem::sim
