#include "net_thread.hpp"
#include "log.hpp"
#include "runtime.hpp"

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
  bool lastPaused{};

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
  // restarted is joined again without touching the guest's game.
  void Run(std::stop_token stop) {
    for (;;) {
      Session(stop);
      if (stop.stop_requested() || config.role != Config::Role::Join)
        return;
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
    auto endpoint = config.role == Config::Role::Host
                        ? Endpoint::Host(identity, config.port, error)
                        : Endpoint::Join(identity, config.address, config.port, error);
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
    if (lastPaused)
      endpoint->SetLocalPaused(true);
    if (config.role == Config::Role::Host)
      log::Info("hosting on UDP {}", config.port);
    else
      log::Info("joining {}:{}", config.address, config.port);

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
                log::Info("in the session as player {} (host {})", e.you, e.host);
                runtime.NotePlayerName(e.you, config.name);
                runtime.PushEvent({ScriptEvent::Kind::InSession, e.you, {}, body});
              } else if constexpr (std::is_same_v<E, events::PlayerJoined>) {
                log::Info("{} joined (player {}, body {})", e.player.name, e.player.id,
                          static_cast<int>(e.player.body));
                runtime.NotePlayerName(e.player.id, e.player.name);
                runtime.PushEvent(
                    {ScriptEvent::Kind::PlayerJoined, e.player.id, e.player.name, e.player.body});
              } else if constexpr (std::is_same_v<E, events::PlayerLeft>) {
                log::Info("player {} left", e.player);
                runtime.ForgetRemote(e.player);
                runtime.PushEvent({ScriptEvent::Kind::PlayerLeft, e.player, {}, {}});
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
              } else if constexpr (std::is_same_v<E, events::HostNpcStates>) {
                runtime.PushNpcStates(e.states);
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
              } else if constexpr (std::is_same_v<E, events::Ended>) {
                log::Info("session ended: {}", e.reason);
                runtime.SetRemotePaused({});
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
      }
      if (auto npcs = runtime.TakeNpcStates())
        endpoint->SendNpcStates(*npcs);
      for (auto &message : runtime.TakeRegroupOutbox()) {
        if (auto *offer = std::get_if<proto::Regroup>(&message))
          endpoint->SendRegroup(std::move(*offer));
        else if (const auto *answer = std::get_if<proto::RegroupAnswer>(&message))
          endpoint->SendRegroupAnswer(*answer);
      }

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
    // Nobody in this session pauses us any more.
    runtime.SetRemotePaused({});
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
