#include "perf.hpp"

#include "tandem/session.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <tuple>
#include <utility>
#include <vector>

namespace tandem::plugin {
namespace {
constexpr const char *kWorkNames[] = {"controller", "pose read", "pose write", "net pass"};
static_assert(std::size(kWorkNames) == static_cast<std::size_t>(Work::Count));
} // namespace

Perf &Perf::Get() {
  static Perf perf;
  // The first report's rates are over the time since the first use (they
  // were over 1 s: "5061/s" for a minute's ticks, 2026-11-26).
  [[maybe_unused]] static const bool started = (perf.m_reportedUs.store(net::NowUs()), true);
  return perf;
}

void Perf::Slot::Add(std::uint64_t value) {
  us.fetch_add(value, std::memory_order_relaxed);
  count.fetch_add(1, std::memory_order_relaxed);
  auto max = maxUs.load(std::memory_order_relaxed);
  while (value > max && !maxUs.compare_exchange_weak(max, value, std::memory_order_relaxed)) {
  }
}

void Perf::Mark(int section) {
  const auto now = net::NowUs();
  if (m_section >= 0 && static_cast<std::size_t>(m_section) < kTickSectionCount)
    m_sections[static_cast<std::size_t>(m_section)].Add(now - m_sectionStartUs);
  if (section == 0)
    m_tickStartUs = now;
  if (section < 0 && m_tickStartUs) {
    m_tick.Add(now - m_tickStartUs);
    m_tickStartUs = 0;
  }
  m_section = section;
  m_sectionStartUs = now;
}

void Perf::Add(Work work, std::uint64_t us) { m_work[static_cast<std::size_t>(work)].Add(us); }

Perf::Scope::Scope(Work work) : m_work(work), m_startUs(net::NowUs()) {}
Perf::Scope::~Scope() { Perf::Get().Add(m_work, net::NowUs() - m_startUs); }

std::string Perf::Report() {
  const auto now = net::NowUs();
  const auto last = m_reportedUs.exchange(now);
  const double s = last && now > last ? static_cast<double>(now - last) / 1e6 : 1.0;
  const auto take = [](Slot &slot) {
    return std::tuple{slot.us.exchange(0), slot.count.exchange(0), slot.maxUs.exchange(0)};
  };
  std::string out;
  const auto [tickUs, ticks, tickMax] = take(m_tick);
  if (ticks)
    out = std::format("script tick {:.3f} ms mean, {:.2f} ms max, {:.0f}/s",
                      static_cast<double>(tickUs) / static_cast<double>(ticks) / 1000.0,
                      static_cast<double>(tickMax) / 1000.0, static_cast<double>(ticks) / s);
  else
    out = "no script tick";
  // The costliest sections, by mean per tick.
  std::vector<std::pair<double, std::string>> sections;
  for (std::size_t i = 0; i < kTickSectionCount; ++i) {
    const auto [us, count, max] = take(m_sections[i]);
    if (count && ticks)
      sections.emplace_back(
          static_cast<double>(us) / static_cast<double>(ticks),
          std::format("{} {:.3f} (max {:.2f})", kTickSections[i],
                      static_cast<double>(us) / static_cast<double>(ticks) / 1000.0,
                      static_cast<double>(max) / 1000.0));
  }
  std::ranges::sort(sections, std::greater<>{});
  if (!sections.empty()) {
    out += "; sections (ms a tick):";
    for (std::size_t i = 0; i < std::min<std::size_t>(sections.size(), 6); ++i)
      out += std::format("{} {}", i ? "," : "", sections[i].second);
  }
  std::string work;
  for (std::size_t i = 0; i < static_cast<std::size_t>(Work::Count); ++i) {
    const auto [us, count, max] = take(m_work[i]);
    if (count)
      work += std::format("{}{} {:.1f} us x {:.0f}/s (max {:.2f} ms)", work.empty() ? "" : ", ",
                          kWorkNames[i], static_cast<double>(us) / static_cast<double>(count),
                          static_cast<double>(count) / s, static_cast<double>(max) / 1000.0);
  }
  if (!work.empty())
    out += "; native: " + work;
  return out;
}
} // namespace tandem::plugin
