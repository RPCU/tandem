#include "stall.hpp"

#include "runtime.hpp"
#include "threads.hpp"

#include "tandem/session.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <thread>
#include <utility>

namespace tandem::plugin::stall {
namespace {
constexpr std::uint64_t kSilentUs = 5'000'000;
constexpr std::uint32_t kMaxFrames = 32;

std::atomic<std::uint64_t> g_netBeatUs{0}; // 0: no net thread
std::atomic<std::uint32_t> g_netThread{0};
std::atomic<std::uint8_t> g_netStep{0};

HANDLE g_file = INVALID_HANDLE_VALUE;
std::jthread g_watcher;

const char *StepName(std::uint8_t step) {
  switch (step) {
  case kSession:
    return "starting a session";
  case kPass:
    return "the top of a pass";
  case kUpdate:
    return "Endpoint::Update and its events";
  case kTake:
    return "taking what the game queued";
  case kReport:
    return "the status and the reports";
  case kRetry:
    return "between sessions";
  default:
    return "?";
  }
}

// One line, straight to the file: no CRT stream, no logger, no lock of ours.
template <class... Args> void Line(std::format_string<Args...> format, Args &&...args) {
  if (g_file == INVALID_HANDLE_VALUE)
    return;
  char buffer[512];
  SYSTEMTIME now{};
  GetLocalTime(&now);
  constexpr std::ptrdiff_t room = sizeof(buffer) - 1;
  auto head = std::format_to_n(
      buffer, room, "[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] stall: ", now.wYear, now.wMonth,
      now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
  auto body =
      std::format_to_n(head.out, room - (head.out - buffer), format, std::forward<Args>(args)...);
  *body.out = '\n';
  DWORD written = 0;
  WriteFile(g_file, buffer, static_cast<DWORD>(body.out + 1 - buffer), &written, nullptr);
}

// The thread's registers, stopped only for GetThreadContext: nothing that
// may wait on a lock it holds (the heap, the loader) runs while it is.
bool ContextOf(std::uint32_t id, CONTEXT &context) {
  HANDLE thread =
      OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
  if (!thread)
    return false;
  bool ok = false;
  if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    ok = GetThreadContext(thread, &context) != 0;
    ResumeThread(thread);
  }
  CloseHandle(thread);
  return ok;
}

// Its return addresses by the modules' unwind data (guarded: a thread that
// moved on since leaves a stack that no longer matches).
std::uint32_t Walk(CONTEXT &context, std::uint64_t *pcs) {
  std::uint32_t count = 0;
  __try {
    while (count < kMaxFrames && context.Rip) {
      pcs[count++] = context.Rip;
      DWORD64 base = 0;
      auto *entry = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
      if (!entry) { // a leaf: its return address is on top of the stack
        context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
        context.Rsp += 8;
        continue;
      }
      void *data = nullptr;
      DWORD64 frame = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, entry, &context, &data, &frame,
                       nullptr);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return count;
}

void Frame(std::uint32_t index, std::uint64_t pc) {
  HMODULE module = nullptr;
  wchar_t path[MAX_PATH]{};
  char name[64]{};
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(pc), &module) &&
      GetModuleFileNameW(module, path, MAX_PATH)) {
    const wchar_t *base = path;
    for (const wchar_t *c = path; *c; ++c)
      if (*c == L'\\' || *c == L'/')
        base = c + 1;
    std::size_t i = 0;
    for (; base[i] && i + 1 < sizeof(name); ++i)
      name[i] = base[i] < 128 ? static_cast<char>(base[i]) : '?';
  }
  if (name[0])
    Line("  #{} {}+{:#x}", index, name, pc - reinterpret_cast<std::uint64_t>(module));
  else
    Line("  #{} {:#x}", index, pc);
}

struct Watched {
  std::uint32_t id{};
  const char *what{};
  CONTEXT *context{};
  bool ok{};
};

// The stuck threads: their registers first (raw, at once), then their stacks
// with module names (the loader's lock: a thread stuck holding it stops
// this one there, after the lines above).
void Report(std::uint64_t silentUs, std::uint8_t step) {
  const auto now = net::NowUs();
  const auto frameUs = Runtime::Get().LastGameFrameUs();
  const std::uint32_t net = g_netThread.load(), frame = threads::FrameThread(),
                      tick = threads::TickThread();
  Line("the net thread is silent for {:.1f} s ({}); the game's script tick for {:.1f} s; threads: "
       "net {}, frame update {}, script tick {}",
       static_cast<double>(silentUs) / 1e6, StepName(step),
       frameUs ? static_cast<double>(now - frameUs) / 1e6 : -1.0, net, frame, tick);
  alignas(16) CONTEXT contexts[3]{};
  Watched watched[3] = {{net, "net", &contexts[0]},
                        {frame, "frame update", &contexts[1]},
                        {tick != frame ? tick : 0u, "script tick", &contexts[2]}};
  for (auto &w : watched) {
    if (!w.id)
      continue;
    w.ok = ContextOf(w.id, *w.context);
    if (w.ok)
      Line("thread {} ({}): rip {:#x} rsp {:#x}", w.id, w.what, w.context->Rip, w.context->Rsp);
    else
      Line("thread {} ({}): no registers (error {})", w.id, w.what, GetLastError());
  }
  for (auto &w : watched) {
    if (!w.ok)
      continue;
    std::uint64_t pcs[kMaxFrames]{};
    const auto count = Walk(*w.context, pcs);
    Line("thread {} ({}), {} frames:", w.id, w.what, count);
    for (std::uint32_t i = 0; i < count; ++i)
      Frame(i, pcs[i]);
  }
}

void Watch(std::stop_token stop) {
  using namespace std::chrono_literals;
  std::uint64_t reportedBeat = 0; // the beat a report was written for
  while (!stop.stop_requested()) {
    std::this_thread::sleep_for(250ms);
    const auto beat = g_netBeatUs.load();
    if (!beat) {
      reportedBeat = 0;
      continue;
    }
    const auto now = net::NowUs();
    if (reportedBeat && beat != reportedBeat) {
      Line("the net thread is back after {:.1f} s", static_cast<double>(beat - reportedBeat) / 1e6);
      reportedBeat = 0;
    }
    if (!reportedBeat && now > beat && now - beat > kSilentUs) {
      reportedBeat = beat;
      Report(now - beat, g_netStep.load());
    }
  }
}
} // namespace

void NetBeat(Step step) {
  g_netStep.store(step, std::memory_order_relaxed);
  g_netThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
  g_netBeatUs.store(net::NowUs(), std::memory_order_release);
}

void NetGone() { g_netBeatUs.store(0, std::memory_order_release); }

void Start(const std::filesystem::path &file) {
  if (g_watcher.joinable())
    return;
  // This run's only (a stall ends the run: the game is killed).
  g_file = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  g_watcher = std::jthread(Watch);
}

void Stop() {
  if (g_watcher.joinable()) {
    g_watcher.request_stop();
    g_watcher.join();
  }
  if (g_file != INVALID_HANDLE_VALUE)
    CloseHandle(g_file);
  g_file = INVALID_HANDLE_VALUE;
}
} // namespace tandem::plugin::stall
