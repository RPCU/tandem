#include "net_thread.hpp"
#include "log.hpp"
#include "runtime.hpp"
#include "story.hpp"

#include "tandem/endpoint.hpp"

#include <optional>
#include <thread>

namespace tandem::plugin {
using namespace std::chrono_literals;

struct NetThread::Impl {
  Config config;
  proto::Body body{};
  std::jthread thread;
  // What we last handed an endpoint that it keeps for the session: a new
  // session (a guest joining again) starts from them.
  std::optional<proto::Appearance> lastAppearance;
  std::optional<proto::Held> lastHeld;
  std::optional<proto::Seat> lastSeat;
  std::optional<proto::VehicleRadio> lastRadio;
  std::optional<proto::Stealth> lastStealth;
  bool lastPaused{};
  std::optional<proto::TimeEffect> lastTime;
  // Guest through a relay: the session's secret, to join again after the
  // code expired.
  std::optional<net::RelaySecret> relaySecret;

  // Once in the session, joining again goes by its secret alone: the code
  // has likely expired, and a host that restarted has a new code anyway (a
  // stale code would only spend the relay's lookups).
  std::optional<net::RelayOptions> RelayOf() const {
    if (config.relay.empty())
      return std::nullopt;
    return net::RelayOptions{.address = config.relay,
                             .port = config.relayPort,
                             .token = config.relayToken,
                             .secret = relaySecret,
                             .code = relaySecret ? std::string() : config.joinCode};
  }

  // The relay link's state for scripts (Runtime::RelayStatus).
  static std::string StatusText(const net::RelayStatus &status) {
    if (status.state == net::RelayStatus::State::Ready)
      return "ready";
    return status.error.empty() ? "connecting" : status.error;
  }

  std::optional<proto::Vitals> lastVitals;

  // Every other player in the session with its life (up unless it said
  // otherwise), for the game thread (Revive.reds). Cheap: the runtime keeps
  // it only when it changed.
  static void PublishLives(const Endpoint &endpoint) {
    std::vector<proto::Vitals> others;
    for (const auto &[id, info] : endpoint.Players()) {
      if (id == endpoint.You())
        continue;
      const auto it = endpoint.Lives().find(id);
      others.push_back(it != endpoint.Lives().end() ? it->second : proto::Vitals{.player = id});
      others.back().revision = 0; // only life and flags matter to the scripts
    }
    Runtime::Get().SetOthersLives(std::move(others));
  }

  // The other players in their pause menu, for the game thread.
  static void PublishPaused(const Endpoint &endpoint) {
    std::vector<proto::PlayerId> others;
    for (const auto player : endpoint.Paused())
      if (player != endpoint.You())
        others.push_back(player);
    Runtime::Get().SetRemotePaused(std::move(others));
  }

  // A guest whose session ends (host gone, unreachable, timed out) tries
  // again every 5 s for as long as the game runs: a host that crashed or
  // restarted is joined again without touching the guest's game. A join
  // code that never led into a session (wrong, expired) is tried 3 times:
  // each try costs one of the relay's limited code lookups.
  void Run(std::stop_token stop) {
    for (int codeTries = 1;; ++codeTries) {
      Session(stop);
      if (stop.stop_requested() || config.role != Config::Role::Join)
        return;
      if (!config.joinCode.empty() && !relaySecret && codeTries >= 3) {
        log::Warn("not joining again: code {} led to no session",
                  net::DisplayJoinCode(config.joinCode));
        return;
      }
      for (int i = 0; i < 50 && !stop.stop_requested(); ++i)
        std::this_thread::sleep_for(100ms);
      if (stop.stop_requested())
        return;
      log::Info("joining again");
    }
  }

  void Session(std::stop_token stop) {
    auto &runtime = Runtime::Get();
    std::string error;
    const Identity identity{config.name, body};
    const auto relay = RelayOf();
    // A guest with a join code goes through the relay, after the address
    // when tandem.ini gives one; without a code, to the address only.
    const bool byCode = !config.joinCode.empty();
    const JoinTarget target{.address = !byCode || config.addressSet ? config.address : "",
                            .port = config.port,
                            .relay = byCode ? relay : std::nullopt};
    std::unique_ptr<Endpoint> endpoint;
    if (config.role == Config::Role::Host)
      endpoint = Endpoint::Host(identity, config.port, error, {}, relay);
    else if (byCode && !relay)
      error = "a join code needs a relay (relay = host:port in tandem.ini)";
    else
      endpoint = Endpoint::Join(identity, target, error);
    if (!endpoint) {
      log::Error("could not start the session: {}", error);
      runtime.PushEvent({ScriptEvent::Kind::SessionEnded, 0, error, {}});
      return;
    }
    if (lastAppearance)
      endpoint->SetLocalAppearance(*lastAppearance);
    if (lastHeld)
      endpoint->SetLocalHeld(*lastHeld);
    if (lastSeat)
      endpoint->SetLocalSeat(*lastSeat);
    if (lastRadio)
      endpoint->SetLocalVehicleRadio(*lastRadio);
    if (lastStealth)
      endpoint->SetLocalStealth(*lastStealth);
    if (lastPaused)
      endpoint->SetLocalPaused(true);
    if (lastTime)
      endpoint->SetLocalTimeEffect(*lastTime);
    if (lastVitals)
      endpoint->SetLocalVitals(*lastVitals);
    if (config.role == Config::Role::Host)
      log::Info("hosting on UDP {}{}", config.port,
                relay ? std::format(", and through relay {}:{}", relay->address, relay->port) : "");
    else if (target.address.empty())
      log::Info("joining code {} through relay {}:{}", net::DisplayJoinCode(config.joinCode),
                config.relay, config.relayPort);
    else
      log::Info("joining {}:{}{}", config.address, config.port,
                target.relay ? " (through the relay if it does not answer)" : "");
    // The relay as last logged and handed to scripts.
    std::optional<net::RelayStatus> relayShown;

    auto nextReport = std::chrono::steady_clock::now() + 10s;
    bool ended = false;
    std::uint64_t silentSinceUs = 0;

    while (!stop.stop_requested() && !ended) {
      // Watchdog (runtime.hpp): one line when the game thread stops ticking,
      // one when it comes back. Pause menus and loading screens stop it too.
      if (const auto last = runtime.LastGameFrameUs(); last) {
        const auto now = net::NowUs();
        if (!silentSinceUs && now - last > 5'000'000) {
          silentSinceUs = last;
          log::Warn("watchdog: game thread silent for 5 s (crashed, hung, paused or loading)");
        } else if (silentSinceUs && now - last < 1'000'000) {
          log::Info("watchdog: game thread back after {:.1f} s",
                    static_cast<double>(last - silentSinceUs) / 1e6);
          silentSinceUs = 0;
        }
      }
      // 2 ms: states go out within a frame of the game handing them over.
      for (const auto &event : endpoint->Update(2ms)) {
        std::visit(
            [&](const auto &e) {
              using E = std::decay_t<decltype(e)>;
              if constexpr (std::is_same_v<E, events::Joined>) {
                log::Info("in the session as player {} (host {}{})", e.you, e.host,
                          endpoint->ThroughRelay() ? ", through the relay" : "");
                if (const auto secret = endpoint->RelaySessionSecret())
                  relaySecret = secret;
                runtime.NotePlayerName(e.you, config.name);
                runtime.SetYou(e.you);
                runtime.PushEvent({ScriptEvent::Kind::InSession, e.you, {}, body});
                Story::Get().PushInbound(
                    {.kind = StoryInbound::Kind::InSession, .player = e.you, .host = e.host});
              } else if constexpr (std::is_same_v<E, events::PlayerJoined>) {
                log::Info("{} joined (player {}, body {})", e.player.name, e.player.id,
                          static_cast<int>(e.player.body));
                runtime.NotePlayerName(e.player.id, e.player.name);
                runtime.PushEvent(
                    {ScriptEvent::Kind::PlayerJoined, e.player.id, e.player.name, e.player.body});
                Story::Get().PushInbound(
                    {.kind = StoryInbound::Kind::Joined, .player = e.player.id});
              } else if constexpr (std::is_same_v<E, events::PlayerLeft>) {
                log::Info("player {} left", e.player);
                runtime.ForgetRemote(e.player);
                runtime.PushEvent({ScriptEvent::Kind::PlayerLeft, e.player, {}, {}});
                Story::Get().PushInbound({.kind = StoryInbound::Kind::Left, .player = e.player});
              } else if constexpr (std::is_same_v<E, events::PlayerReturned>) {
                // Its puppet and seat stay; its new game sends its look again.
                log::Info("{} is back on a new connection (player {})", e.player.name, e.player.id);
              } else if constexpr (std::is_same_v<E, events::RemoteState>) {
                runtime.PushRemote(e.state);
              } else if constexpr (std::is_same_v<E, events::RemotePose>) {
                runtime.PushRemotePose(e.pose);
              } else if constexpr (std::is_same_v<E, events::RemoteHeld>) {
                runtime.PushRemoteHeld(e.held);
              } else if constexpr (std::is_same_v<E, events::RemoteShot>) {
                runtime.PushRemoteShot(e.shot);
              } else if constexpr (std::is_same_v<E, events::RemoteSeat>) {
                log::Info("player {}: seat {:#x} in vehicle {:#x} of player {}", e.seat.player,
                          e.seat.seat, e.seat.vehicle, e.seat.owner);
                runtime.PushRemoteSeat(e.seat);
              } else if constexpr (std::is_same_v<E, events::RemoteVehicle>) {
                runtime.PushRemoteVehicle(e.vehicle);
              } else if constexpr (std::is_same_v<E, events::RemoteVehicleRadio>) {
                log::Info("player {}: car radio {}, station {}, track {:#x}{}", e.radio.player,
                          e.radio.on ? "on" : "off", e.radio.station, e.radio.track,
                          e.radio.trackStartUs ? " (start known)" : "");
                runtime.PushRemoteRadio(e.radio);
              } else if constexpr (std::is_same_v<E, events::HostWorld>) {
                runtime.PushHostWorld(e.world);
              } else if constexpr (std::is_same_v<E, events::HostNpcSpawn>) {
                runtime.PushNpcEvent({NpcEvent::Kind::Spawn, e.spawn, 0});
              } else if constexpr (std::is_same_v<E, events::HostNpcGone>) {
                runtime.PushNpcEvent({.kind = NpcEvent::Kind::Gone,
                                      .spawn = {.npc = e.gone.npc},
                                      .reason = e.gone.reason,
                                      .player = e.gone.killer});
                runtime.ForgetRemote(kNpcPuppetBase + e.gone.npc);
              } else if constexpr (std::is_same_v<E, events::HostNpcHealth>) {
                runtime.PushNpcEvent({.kind = NpcEvent::Kind::Health,
                                      .spawn = {.npc = e.health.npc},
                                      .amount = e.health.health});
              } else if constexpr (std::is_same_v<E, events::NpcEffectBy>) {
                for (const auto effect : e.effect.effects)
                  runtime.PushNpcEvent({.kind = NpcEvent::Kind::Effect,
                                        .spawn = {.npc = e.effect.npc, .record = effect},
                                        .player = e.effect.player});
              } else if constexpr (std::is_same_v<E, events::NpcHitBy>) {
                runtime.PushNpcEvent({.kind = NpcEvent::Kind::Hit,
                                      .spawn = {.npc = e.hit.npc, .position = e.hit.position},
                                      .player = e.hit.player,
                                      .amount = e.hit.damage,
                                      .flags = e.hit.flags,
                                      .direction = e.hit.direction});
              } else if constexpr (std::is_same_v<E, events::NpcActionBy>) {
                const auto &a = e.action;
                runtime.PushActionEvent({.kind = ActionEvent::Kind::Request,
                                         .npc = a.npc,
                                         .player = a.player,
                                         .action = static_cast<std::uint8_t>(a.kind),
                                         .name = a.name,
                                         .item = a.item,
                                         .amount = a.amount,
                                         .direction = a.direction});
              } else if constexpr (std::is_same_v<E, events::NpcActionAnswered>) {
                const auto &r = e.reply;
                runtime.PushActionEvent({.kind = ActionEvent::Kind::Reply,
                                         .npc = r.npc,
                                         .action = static_cast<std::uint8_t>(r.kind),
                                         .accepted = r.accepted,
                                         .reason = r.reason});
              } else if constexpr (std::is_same_v<E, events::HostNpcHold>) {
                const auto &h = e.hold;
                runtime.PushActionEvent({.kind = ActionEvent::Kind::Hold,
                                         .npc = h.npc,
                                         .player = h.player,
                                         .action = static_cast<std::uint8_t>(h.how),
                                         .name = h.name});
              } else if constexpr (std::is_same_v<E, events::HostNpcCue>) {
                runtime.PushActionEvent({.kind = ActionEvent::Kind::Cue,
                                         .npc = e.cue.npc,
                                         .player = e.cue.player,
                                         .action = static_cast<std::uint8_t>(e.cue.cue)});
              } else if constexpr (std::is_same_v<E, events::NpcHackBy>) {
                runtime.PushHackEvent({.kind = HackEvent::Kind::Upload,
                                       .npc = e.hack.npc,
                                       .player = e.hack.player,
                                       .action = e.hack.action,
                                       .flags = e.hack.flags,
                                       .seconds = static_cast<float>(e.hack.uploadMs) / 1000.f});
              } else if constexpr (std::is_same_v<E, events::HostNpcHackState>) {
                runtime.PushHackEvent({.kind = HackEvent::Kind::State,
                                       .npc = e.state.npc,
                                       .player = e.state.player,
                                       .action = e.state.action,
                                       .state = static_cast<std::int32_t>(e.state.step),
                                       .seconds = static_cast<float>(e.state.uploadMs) / 1000.f});
              } else if constexpr (std::is_same_v<E, events::HostNpcStatus>) {
                runtime.PushHackEvent({.kind = HackEvent::Kind::Status,
                                       .npc = e.status.npc,
                                       .player = e.status.player,
                                       .action = e.status.effect,
                                       .state = e.status.applied ? 1 : 0});
              } else if constexpr (std::is_same_v<E, events::HostPlayerHack>) {
                log::Info("npc {} hacks our V ({:#x})", e.hack.npc, e.hack.action);
                runtime.PushHackEvent({.kind = HackEvent::Kind::PlayerHack,
                                       .npc = e.hack.npc,
                                       .player = e.hack.player,
                                       .action = e.hack.action,
                                       .flags = e.hack.flags});
              } else if constexpr (std::is_same_v<E, events::VehicleHackOn>) {
                log::Info("player {} hacked the car we drive ({})", e.hack.player,
                          static_cast<int>(e.hack.hack));
                runtime.PushHackEvent({.kind = HackEvent::Kind::Car,
                                       .player = e.hack.player,
                                       .state = static_cast<std::int32_t>(e.hack.hack),
                                       .seconds = e.hack.seconds});
              } else if constexpr (std::is_same_v<E, events::HitByNpc>) {
                runtime.PushPlayerHit(e.hit);
              } else if constexpr (std::is_same_v<E, events::HostNpcStates>) {
                runtime.PushNpcStates(e.states, endpoint->You());
              } else if constexpr (std::is_same_v<E, events::HostNpcAwareness>) {
                runtime.PushNpcAwareness(e.awareness, endpoint->You());
              } else if constexpr (std::is_same_v<E, events::RemoteStealth>) {
                log::Info("player {}: stealth flags {:#x}, visibility {:.2f}, camo {:#x}",
                          e.stealth.player, e.stealth.flags, e.stealth.visibility, e.stealth.camo);
                runtime.PushRemoteStealth(e.stealth);
              } else if constexpr (std::is_same_v<E, events::NoiseBy>) {
                runtime.PushRemoteNoise(e.noise);
              } else if constexpr (std::is_same_v<E, events::RemoteAppearance>) {
                log::Info("player {}: look of {} keys and {} items (revision {})",
                          e.appearance.player, e.appearance.look.size(), e.appearance.items.size(),
                          e.appearance.revision);
                runtime.PushRemoteAppearance(e.appearance);
              } else if constexpr (std::is_same_v<E, events::PauseChanged>) {
                log::Info("player {} {} (pause menu)", e.pause.player,
                          e.pause.paused ? "paused" : "resumed");
                PublishPaused(*endpoint);
              } else if constexpr (std::is_same_v<E, events::RegroupOffered>) {
                log::Info("player {} offers to join them (offer {}, reason {}, point {:#x}, at "
                          "({:.1f}, {:.1f}, {:.1f}))",
                          e.offer.from, e.offer.offer, static_cast<int>(e.offer.reason),
                          e.offer.point, e.offer.position.x, e.offer.position.y,
                          e.offer.position.z);
                runtime.PushRegroupEvent({.kind = RegroupEvent::Kind::Offer, .offer = e.offer});
              } else if constexpr (std::is_same_v<E, events::RegroupAnswered>) {
                log::Info("player {} {} our offer {}", e.answer.from,
                          e.answer.accepted ? "accepted" : "declined", e.answer.offer);
                runtime.PushRegroupEvent({.kind = RegroupEvent::Kind::Answer, .answer = e.answer});
              } else if constexpr (std::is_same_v<E, events::RemoteVitals>) {
                log::Info("player {}: life {} (flags {:#x})", e.vitals.player,
                          static_cast<int>(e.vitals.life), e.vitals.flags);
              } else if constexpr (std::is_same_v<E, events::ReviveBy>) {
                log::Info("player {} revives our V: step {}", e.revive.player,
                          static_cast<int>(e.revive.step));
                runtime.PushRevive(e.revive);
              } else if constexpr (std::is_same_v<E, events::SessionLost>) {
                log::Info("session lost: no V is up (loss {})", e.over.round);
                runtime.NoteSessionLost();
              } else if constexpr (std::is_same_v<E, events::Story>) {
                Story::Get().PushInbound(
                    {.kind = StoryInbound::Kind::Message, .message = e.message});
              } else if constexpr (std::is_same_v<E, events::RemoteTimeEffect>) {
                log::Info("player {}: time abilities {:#x} (world speed {:.2f})", e.effect.player,
                          e.effect.abilities, e.effect.scale);
                runtime.PushRemoteTime(e.effect);
              } else if constexpr (std::is_same_v<E, events::WorldItemAppeared>) {
                log::Info("loot: world item {} dropped by player {} ({} stacks, token {})",
                          e.item.id, e.item.dropper, e.item.stacks.size(), e.item.token);
                runtime.PushLoot(e.item);
              } else if constexpr (std::is_same_v<E, events::WorldItemTakenBy>) {
                log::Info("loot: world item {} stack {} taken by player {}", e.taken.id,
                          e.taken.stack, e.taken.taker);
                runtime.PushLoot(e.taken);
              } else if constexpr (std::is_same_v<E, events::SharedItemFrom>) {
                log::Info("loot: player {} shares item {:#x} x{} ({}, npc {})", e.item.player,
                          e.item.item.record, e.item.item.quantity,
                          e.item.kind == proto::kSharedOnCorpse ? "on a corpse" : "looted",
                          e.item.npc);
                runtime.PushLoot(e.item);
              } else if constexpr (std::is_same_v<E, events::Ended>) {
                Story::Get().PushInbound({.kind = StoryInbound::Kind::Ended});
                log::Info("session ended: {}", e.reason);
                runtime.SetYou(0);
                runtime.SetRemotePaused({});
                runtime.SetOthersLives({});
                // The next session's players start afresh (a restarted host
                // counts its states from the start again).
                for (const auto &[id, info] : endpoint->Players())
                  if (id != endpoint->You())
                    runtime.ForgetRemote(id);
                runtime.PushEvent({ScriptEvent::Kind::SessionEnded, 0, e.reason, {}});
                ended = true;
              }
            },
            event);
      }

      if (auto status = endpoint->Relay()) {
        status->codeSecondsLeft = 0; // not a change worth a line
        if (status != relayShown) {
          if (!status->code.empty() && (!relayShown || relayShown->code != status->code))
            log::Info("join code {} (relay {}:{})", net::DisplayJoinCode(status->code),
                      config.relay, config.relayPort);
          if (!status->error.empty() && (!relayShown || relayShown->error != status->error))
            log::Warn("relay: {}", status->error);
          else if (status->state == net::RelayStatus::State::Ready &&
                   (!relayShown || relayShown->state != status->state))
            log::Info("relay: registered");
          runtime.SetRelay(status->code.empty() ? "" : net::DisplayJoinCode(status->code),
                           StatusText(*status));
          relayShown = status;
        }
      }

      runtime.SetClockOffset(static_cast<std::int64_t>(endpoint->HostNowUs()) -
                             static_cast<std::int64_t>(net::NowUs()));
      if (auto local = runtime.TakeLocal())
        endpoint->SendLocalState(*local);
      if (auto pose = runtime.TakeLocalPose())
        endpoint->SendLocalPose(*pose);
      if (auto appearance = runtime.TakeLocalAppearance()) {
        lastAppearance = *appearance;
        endpoint->SetLocalAppearance(std::move(*appearance));
      }
      if (auto held = runtime.TakeLocalHeld()) {
        lastHeld = *held;
        endpoint->SetLocalHeld(*held);
      }
      for (const auto &shot : runtime.TakeLocalShots())
        endpoint->SendLocalShot(shot);
      if (auto seat = runtime.TakeLocalSeat()) {
        lastSeat = *seat;
        endpoint->SetLocalSeat(*seat);
      }
      if (auto vehicle = runtime.TakeLocalVehicle())
        endpoint->SendLocalVehicle(*vehicle);
      for (const auto &radio : runtime.TakeLocalRadios()) {
        lastRadio = radio;
        endpoint->SetLocalVehicleRadio(radio);
      }
      if (auto paused = runtime.TakeLocalPaused()) {
        lastPaused = *paused;
        endpoint->SetLocalPaused(*paused);
        log::Info("our pause menu {}", *paused ? "opened: pausing everyone" : "closed");
      }
      if (auto time = runtime.TakeLocalTime()) {
        lastTime = *time;
        endpoint->SetLocalTimeEffect(*time);
        log::Info("our time abilities {:#x} (world speed {:.2f})", time->abilities, time->scale);
      }
      if (auto world = runtime.TakeWorld())
        endpoint->SetWorld(*world);
      for (const auto &message : runtime.TakeNpcOutbox()) {
        if (const auto *spawn = std::get_if<proto::NpcSpawn>(&message))
          endpoint->SendNpcSpawn(*spawn);
        else if (const auto *gone = std::get_if<proto::NpcGone>(&message))
          endpoint->SendNpcGone(*gone);
        else if (const auto *health = std::get_if<proto::NpcHealth>(&message))
          endpoint->SendNpcHealth(*health);
        else if (const auto *hit = std::get_if<proto::NpcHit>(&message))
          endpoint->SendNpcHit(*hit);
        else if (const auto *effect = std::get_if<proto::NpcEffect>(&message))
          endpoint->SendNpcEffect(*effect);
        else if (const auto *action = std::get_if<proto::NpcAction>(&message))
          endpoint->SendNpcAction(*action);
        else if (const auto *hold = std::get_if<proto::NpcHold>(&message))
          endpoint->SendNpcHold(*hold);
        else if (const auto *cue = std::get_if<proto::NpcCue>(&message))
          endpoint->SendNpcCue(*cue);
        else if (const auto *hit = std::get_if<proto::PlayerHit>(&message);
                 hit && !endpoint->SendPlayerHit(*hit))
          log::Info("hit on player {} dropped: not a guest in the session", hit->player);
      }
      for (const auto &message : runtime.TakeHackOutbox()) {
        if (const auto *hack = std::get_if<proto::NpcHack>(&message))
          endpoint->SendNpcHack(*hack);
        else if (const auto *state = std::get_if<proto::NpcHackState>(&message))
          endpoint->SendNpcHackState(*state);
        else if (const auto *status = std::get_if<proto::NpcStatus>(&message))
          endpoint->SendNpcStatus(*status);
        else if (const auto *player = std::get_if<proto::PlayerHack>(&message))
          endpoint->SendPlayerHack(*player);
        else if (const auto *car = std::get_if<proto::VehicleHack>(&message))
          endpoint->SendVehicleHack(*car);
      }
      for (const auto &[player, reply] : runtime.TakeNpcReplies())
        endpoint->SendNpcActionReply(player, reply);
      if (auto npcs = runtime.TakeNpcStates())
        endpoint->SendNpcStates(*npcs);
      if (auto awareness = runtime.TakeNpcAwareness())
        endpoint->SendNpcAwareness(std::move(*awareness));
      if (auto stealth = runtime.TakeLocalStealth()) {
        lastStealth = *stealth;
        endpoint->SetLocalStealth(*stealth);
      }
      for (const auto &noise : runtime.TakeLocalNoises())
        endpoint->SendNoise(noise);
      for (auto &[message, to] : Story::Get().TakeOutbound())
        endpoint->SendStory(std::move(message), to);
      for (auto &message : runtime.TakeRegroupOutbox()) {
        if (auto *offer = std::get_if<proto::Regroup>(&message))
          endpoint->SendRegroup(std::move(*offer));
        else if (const auto *answer = std::get_if<proto::RegroupAnswer>(&message))
          endpoint->SendRegroupAnswer(*answer);
      }
      for (auto &message : runtime.TakeLootOutbox()) {
        if (auto *item = std::get_if<proto::WorldItem>(&message))
          endpoint->DropWorldItem(std::move(*item));
        else if (const auto *taken = std::get_if<proto::WorldItemTaken>(&message))
          endpoint->TakeWorldItem(taken->id, taken->stack);
        else if (const auto *shared = std::get_if<proto::SharedItem>(&message))
          endpoint->SendSharedItem(*shared);
      }
      if (auto vitals = runtime.TakeLocalVitals()) {
        lastVitals = *vitals;
        endpoint->SetLocalVitals(*vitals);
        log::Info("our V: life {} (flags {:#x})", static_cast<int>(vitals->life), vitals->flags);
      }
      for (const auto &revive : runtime.TakeLocalRevives())
        endpoint->SendRevive(revive);
      if (!ended)
        PublishLives(*endpoint);

      if (const auto now = std::chrono::steady_clock::now(); now >= nextReport) {
        nextReport = now + 10s;
        const auto link = endpoint->HostLinkStats();
        log::Info(
            "{} player(s){}", endpoint->Players().size(),
            link ? std::format(", rtt {} ms, loss {:.1f}%", link->rttMs, link->packetLoss * 100)
                 : "");
        for (const auto &[id, info] : endpoint->Players()) {
          if (id == endpoint->You())
            continue;
          if (const auto stats = runtime.StatsOf(id))
            log::Info("  player {}: {} states, {} dropped, {} poses, drawn {} ms behind (jitter "
                      "{} ms, transit {} ms, every {} ms)",
                      id, stats->received, stats->dropped, runtime.PosesReceived(id),
                      stats->delayUs / 1000, stats->jitterUs / 1000, stats->transitUs / 1000,
                      stats->intervalUs / 1000);
        }
      }
    }
    runtime.SetYou(0);
    // Nobody in this session pauses us any more; its code is gone.
    runtime.SetRemotePaused({});
    runtime.SetRelay("", "");
    endpoint->Leave("the game closed");
  }
};

NetThread::NetThread(Config config, proto::Body body) : m_impl(std::make_unique<Impl>()) {
  m_impl->config = std::move(config);
  m_impl->body = body;
  m_impl->thread = std::jthread([impl = m_impl.get()](std::stop_token stop) { impl->Run(stop); });
}

NetThread::~NetThread() {
  m_impl->thread.request_stop();
  if (m_impl->thread.joinable())
    m_impl->thread.join();
}
} // namespace tandem::plugin
