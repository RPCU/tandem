#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "referee_expect.hpp"
#include "tandem/sim/referee.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

// The whole suite under the referee (docs/architecture/sim.md): every
// endpoint a test makes is watched; a violation fails the test unless the
// test named its rule (tandem::test::ExpectReferee: it breaks it on
// purpose). What the referee found is listed by test at the end
// (`referee-suite: ...`); TANDEM_REFEREE_SUITE=0 turns it off, =1 also
// prints examples.
namespace {
struct RefereeListener;
RefereeListener *g_listener{};

struct RefereeListener : doctest::IReporter {
  explicit RefereeListener(const doctest::ContextOptions &) { g_listener = this; }

  void test_run_start() override {
    const char *off = std::getenv("TANDEM_REFEREE_SUITE");
    if (off && std::string(off) == "0")
      return;
    tandem::Endpoint::SetCreationHook([this](tandem::Endpoint &endpoint) {
      auto referee = std::make_shared<tandem::sim::Referee>(endpoint.IsHost());
      referee->Attach(endpoint);
      referee->OnFinding([this](const tandem::sim::Finding &finding) { Found(finding); });
      const std::lock_guard lock(m_mutex);
      m_referees.push_back(std::move(referee));
    });
  }
  void test_case_start(const doctest::TestCaseData &test) override {
    const std::lock_guard lock(m_mutex);
    m_test = test.m_name;
    m_expected.clear();
    m_failed.clear();
  }
  void test_case_end(const doctest::CurrentTestCaseStats &) override {
    const std::lock_guard lock(m_mutex);
    auto &found = m_found[m_test];
    for (const auto &referee : m_referees) {
      ++m_watched;
      for (const auto &[rule, count] : referee->Counts())
        found[rule] += count;
      m_violations += referee->Violations();
      m_hints += referee->Hints();
      for (const auto &finding : referee->Take())
        if (m_examples.size() < 40 && !finding.hint)
          m_examples.push_back(m_test + ": " + finding.Line());
    }
    m_referees.clear();
    if (found.empty())
      m_found.erase(m_test);
  }
  void test_run_end(const doctest::TestRunStats &) override {
    tandem::Endpoint::SetCreationHook({});
    if (m_watched == 0)
      return;
    std::cout << "referee-suite: " << m_watched << " endpoints watched, " << m_violations
              << " violations (" << m_unexpected << " unexpected) and " << m_hints << " hints in "
              << m_found.size() << " tests\n";
    for (const auto &[test, rules] : m_found) {
      std::cout << "referee-suite:   " << test << ":";
      for (const auto &[rule, count] : rules)
        std::cout << ' ' << rule << " x" << count;
      std::cout << '\n';
    }
    if (std::getenv("TANDEM_REFEREE_SUITE"))
      for (const auto &example : m_examples)
        std::cout << "referee-suite:   " << example << '\n';
  }

  // A finding, as the referee makes it (on the test's thread).
  void Found(const tandem::sim::Finding &finding) {
    {
      const std::lock_guard lock(m_mutex);
      if (finding.hint || m_expected.contains(finding.rule))
        return;
      ++m_unexpected;
      // Once per rule and test: the first says what broke it.
      if (!m_failed.insert(finding.rule).second)
        return;
    }
    const auto line = finding.Line() + " (a test that breaks it on purpose says so: "
                                       "tandem::test::ExpectReferee)";
    ADD_FAIL_CHECK_AT(__FILE__, __LINE__, line);
  }
  void Expect(std::initializer_list<std::string_view> rules) {
    const std::lock_guard lock(m_mutex);
    for (const auto rule : rules)
      m_expected.emplace(rule);
  }

  void report_query(const doctest::QueryData &) override {}
  void test_case_reenter(const doctest::TestCaseData &) override {}
  void test_case_exception(const doctest::TestCaseException &) override {}
  void subcase_start(const doctest::SubcaseSignature &) override {}
  void subcase_end() override {}
  void log_assert(const doctest::AssertData &) override {}
  void log_message(const doctest::MessageData &) override {}
  void test_case_skipped(const doctest::TestCaseData &) override {}

  std::mutex m_mutex;
  std::vector<std::shared_ptr<tandem::sim::Referee>> m_referees;
  std::string m_test;
  std::set<std::string, std::less<>> m_expected, m_failed;
  std::map<std::string, std::map<std::string, std::uint32_t>> m_found;
  std::vector<std::string> m_examples;
  std::size_t m_watched{};
  std::uint64_t m_violations{}, m_hints{}, m_unexpected{};
};
} // namespace

REGISTER_LISTENER("referee", 1, RefereeListener);

namespace tandem::test {
void ExpectReferee(std::initializer_list<std::string_view> rules) {
  if (g_listener)
    g_listener->Expect(rules);
}
} // namespace tandem::test
