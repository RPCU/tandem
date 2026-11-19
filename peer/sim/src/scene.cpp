#include "tandem/sim/scene.hpp"

#include "tandem/sim/names.hpp"

#include <algorithm>

namespace tandem::sim {
namespace p = proto;

void Scene::On(const SessionEvent &event, std::uint64_t nowUs) {
  const auto of = [&](p::PlayerId id) -> SeenPlayer & {
    auto &player = players[id];
    player.id = id;
    return player;
  };
  std::visit(
      [&](const auto &e) {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<E, events::Joined>) {
          self = e.you;
          host = e.host;
        } else if constexpr (std::is_same_v<E, events::PlayerJoined>) {
          of(e.player.id);
        } else if constexpr (std::is_same_v<E, events::PlayerReturned>) {
          players[e.player.id] = SeenPlayer{.id = e.player.id};
        } else if constexpr (std::is_same_v<E, events::PlayerLeft>) {
          players.erase(e.player);
          for (auto &[id, npc] : npcs)
            if (npc.holder == e.player)
              npc.holder = p::kNoPlayer;
        } else if constexpr (std::is_same_v<E, events::RemoteState>) {
          auto &player = of(e.state.player);
          player.heard = true;
          player.heardUs = nowUs;
          player.position = e.state.position;
          player.velocity = e.state.velocity;
          player.yaw = YawOf(e.state.orientation);
        } else if constexpr (std::is_same_v<E, events::RemoteStealth>) {
          auto &player = of(e.stealth.player);
          player.stealth = e.stealth.flags;
          player.visibility = e.stealth.visibility;
        } else if constexpr (std::is_same_v<E, events::RemoteVitals>) {
          of(e.vitals.player).life = e.vitals.life;
        } else if constexpr (std::is_same_v<E, events::RemoteElsewhere>) {
          auto &player = of(e.state.player);
          player.place = e.state.place;
          player.elsewhere = e.state.flags;
        } else if constexpr (std::is_same_v<E, events::PauseChanged>) {
          of(e.pause.player).paused = e.pause.paused;
        } else if constexpr (std::is_same_v<E, events::RemoteTimeEffect>) {
          auto &player = of(e.effect.player);
          player.abilities = e.effect.abilities;
          player.timeScale = e.effect.scale;
        } else if constexpr (std::is_same_v<E, events::RemoteSeat>) {
          auto &player = of(e.seat.player);
          player.seat = e.seat;
          if (e.seat.vehicle == 0)
            player.car.reset();
        } else if constexpr (std::is_same_v<E, events::RemoteVehicle>) {
          of(e.vehicle.player).car = e.vehicle;
        } else if constexpr (std::is_same_v<E, events::RemoteTransit>) {
          of(e.transit.player).transit = e.transit.kind;
        } else if constexpr (std::is_same_v<E, events::Story>) {
          if (const auto *state = std::get_if<p::StoryState>(&e.message)) {
            auto &player = of(state->player);
            player.tier = state->tier;
            player.story = state->flags;
          } else if (const auto *phone = std::get_if<p::PhoneCall>(&e.message)) {
            if (phone->phase == 3)
              call.reset();
            else
              call = *phone;
          } else if (const auto *choices = std::get_if<p::ChoiceHub>(&e.message)) {
            if (choices->options.empty())
              hub.reset();
            else
              hub = *choices;
          } else if (const auto *cursor = std::get_if<p::ChoiceCursor>(&e.message)) {
            if (cursor->kind == p::kCursorPicked)
              hub.reset();
          }
        } else if constexpr (std::is_same_v<E, events::HostNpcSpawn>) {
          npcs[e.spawn.npc] = SeenNpc{.id = e.spawn.npc,
                                      .record = e.spawn.record,
                                      .position = e.spawn.position,
                                      .yaw = e.spawn.yaw};
        } else if constexpr (std::is_same_v<E, events::HostNpcStates>) {
          for (const auto &entry : e.states.npcs)
            if (const auto it = npcs.find(entry.npc); it != npcs.end() && it->second.live) {
              it->second.position = entry.position;
              it->second.yaw = entry.yaw;
              it->second.detected = entry.detected;
            }
        } else if constexpr (std::is_same_v<E, events::HostNpcGone>) {
          const auto it = npcs.find(e.gone.npc);
          if (it == npcs.end())
            return;
          if (e.gone.reason == p::kGoneAway || e.gone.reason == p::kGoneDisposed) {
            npcs.erase(it);
            return;
          }
          it->second.live = false;
          it->second.body = true;
          it->second.dead = it->second.dead || e.gone.reason == p::kGoneDead;
          it->second.detected = 0;
        } else if constexpr (std::is_same_v<E, events::HostNpcHealth>) {
          if (const auto it = npcs.find(e.health.npc); it != npcs.end())
            it->second.health = e.health.health;
        } else if constexpr (std::is_same_v<E, events::HostNpcHold>) {
          if (const auto it = npcs.find(e.hold.npc); it != npcs.end())
            it->second.holder = e.hold.player;
        } else if constexpr (std::is_same_v<E, events::WorldItemAppeared>) {
          const bool empty = std::ranges::all_of(
              e.item.stacks, [](const auto &stack) { return stack.quantity == 0; });
          if (empty)
            items.erase(e.item.id);
          else
            items[e.item.id] = e.item;
        } else if constexpr (std::is_same_v<E, events::WorldItemTakenBy>) {
          const auto it = items.find(e.taken.id);
          if (it == items.end())
            return;
          if (e.taken.taker == p::kNoPlayer) { // gone for everyone
            items.erase(it);
            return;
          }
          if (e.taken.stack < it->second.stacks.size())
            it->second.stacks[e.taken.stack].quantity = 0;
          if (std::ranges::all_of(it->second.stacks,
                                  [](const auto &stack) { return stack.quantity == 0; }))
            items.erase(it);
        } else if constexpr (std::is_same_v<E, events::Ended>) {
          players.clear();
          npcs.clear();
          items.clear();
          call.reset();
          hub.reset();
        }
      },
      event);
}

const SeenPlayer *Scene::Find(p::PlayerId player) const {
  const auto it = players.find(player);
  return it == players.end() ? nullptr : &it->second;
}

const SeenPlayer *Scene::Partner() const {
  if (self != host)
    if (const auto *player = Find(host); player && player->heard)
      return player;
  for (const auto &[id, player] : players)
    if (player.heard)
      return &player;
  return nullptr;
}

bool Scene::AnyoneElseUp() const {
  return std::ranges::any_of(players,
                             [](const auto &entry) { return entry.second.life == p::Life::Up; });
}

bool Deliver(Endpoint &endpoint, const Outgoing &outgoing) {
  const auto &message = outgoing.message;
  if (p::IsStory(message)) {
    endpoint.SendStory(message, outgoing.to);
    return true;
  }
  if (p::IsCombat(message))
    return endpoint.SendCombat(message);
  return std::visit(
      [&](const auto &m) {
        using M = std::decay_t<decltype(m)>;
        // Our own V.
        if constexpr (std::is_same_v<M, p::PlayerState>)
          endpoint.SendLocalState(m);
        else if constexpr (std::is_same_v<M, p::PoseState>)
          endpoint.SendLocalPose(m);
        else if constexpr (std::is_same_v<M, p::Appearance>)
          endpoint.SetLocalAppearance(m);
        else if constexpr (std::is_same_v<M, p::Held>)
          endpoint.SetLocalHeld(m);
        else if constexpr (std::is_same_v<M, p::Shot>)
          endpoint.SendLocalShot(m);
        else if constexpr (std::is_same_v<M, p::Seat>)
          endpoint.SetLocalSeat(m);
        else if constexpr (std::is_same_v<M, p::VehicleState>)
          endpoint.SendLocalVehicle(m);
        else if constexpr (std::is_same_v<M, p::VehicleRadio>)
          endpoint.SetLocalVehicleRadio(m);
        else if constexpr (std::is_same_v<M, p::Pause>)
          endpoint.SetLocalPaused(m.paused);
        else if constexpr (std::is_same_v<M, p::Stealth>)
          endpoint.SetLocalStealth(m);
        else if constexpr (std::is_same_v<M, p::TimeEffect>)
          endpoint.SetLocalTimeEffect(m);
        else if constexpr (std::is_same_v<M, p::Elsewhere>)
          endpoint.SetLocalElsewhere(m);
        else if constexpr (std::is_same_v<M, p::Transit>)
          endpoint.SetLocalTransit(m);
        else if constexpr (std::is_same_v<M, p::Vitals>)
          endpoint.SetLocalVitals(m);
        else if constexpr (std::is_same_v<M, p::Revive>)
          endpoint.SendRevive(m);
        else if constexpr (std::is_same_v<M, p::VehicleHack>)
          endpoint.SendVehicleHack(m);
        else if constexpr (std::is_same_v<M, p::Regroup>)
          endpoint.SendRegroup(m);
        else if constexpr (std::is_same_v<M, p::RegroupAnswer>)
          endpoint.SendRegroupAnswer(m);
        else if constexpr (std::is_same_v<M, p::WorldItem>)
          endpoint.DropWorldItem(m);
        else if constexpr (std::is_same_v<M, p::WorldItemTaken>)
          endpoint.TakeWorldItem(m.id, m.stack);
        else if constexpr (std::is_same_v<M, p::SharedItem>)
          endpoint.SendSharedItem(m);
        else if constexpr (std::is_same_v<M, p::Epoch>)
          endpoint.BumpEpoch(m.reason);
        // A guest's requests to the host.
        else if constexpr (std::is_same_v<M, p::NpcHit>)
          endpoint.SendNpcHit(m);
        else if constexpr (std::is_same_v<M, p::NpcEffect>)
          endpoint.SendNpcEffect(m);
        else if constexpr (std::is_same_v<M, p::NpcHack>)
          endpoint.SendNpcHack(m);
        else if constexpr (std::is_same_v<M, p::NpcAction>)
          endpoint.SendNpcAction(m);
        else if constexpr (std::is_same_v<M, p::Noise>)
          endpoint.SendNoise(m);
        else if constexpr (std::is_same_v<M, p::ShotHit>)
          endpoint.SendShotHit(m);
        // The host's world.
        else if constexpr (std::is_same_v<M, p::World>)
          endpoint.SetWorld(m);
        else if constexpr (std::is_same_v<M, p::NpcSpawn>)
          endpoint.SendNpcSpawn(m);
        else if constexpr (std::is_same_v<M, p::NpcStates>)
          endpoint.SendNpcStates(m);
        else if constexpr (std::is_same_v<M, p::NpcGone>)
          endpoint.SendNpcGone(m);
        else if constexpr (std::is_same_v<M, p::NpcHealth>)
          endpoint.SendNpcHealth(m);
        else if constexpr (std::is_same_v<M, p::NpcAnims>)
          endpoint.SendNpcAnims(m);
        else if constexpr (std::is_same_v<M, p::NpcShot>)
          endpoint.SendNpcShot(m);
        else if constexpr (std::is_same_v<M, p::NpcHackState>)
          endpoint.SendNpcHackState(m);
        else if constexpr (std::is_same_v<M, p::NpcStatus>)
          endpoint.SendNpcStatus(m);
        else if constexpr (std::is_same_v<M, p::PlayerHack>)
          endpoint.SendPlayerHack(m);
        else if constexpr (std::is_same_v<M, p::PlayerHit>)
          endpoint.SendPlayerHit(m);
        else if constexpr (std::is_same_v<M, p::NpcHold>)
          endpoint.SendNpcHold(m);
        else if constexpr (std::is_same_v<M, p::NpcCue>)
          endpoint.SendNpcCue(m);
        else if constexpr (std::is_same_v<M, p::NpcShotAt>)
          endpoint.SendNpcShotAt(m);
        else if constexpr (std::is_same_v<M, p::NpcAwareness>)
          endpoint.SendNpcAwareness(m);
        else if constexpr (std::is_same_v<M, p::NpcSeat>)
          endpoint.SendNpcSeat(m);
        else if constexpr (std::is_same_v<M, p::NpcActionReply>)
          endpoint.SendNpcActionReply(outgoing.to, m);
        else
          return false;
        return true;
      },
      message);
}
} // namespace tandem::sim
