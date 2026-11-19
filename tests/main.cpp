#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "tandem/sim/referee.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The whole suite under the referee (docs/architecture/sim.md): every
// endpoint a test makes is watched, and what the referee found is listed by
// test at the end (`referee-suite: ...`). Tests break the rules on purpose
// (forged senders, injected faults), so it is a report, not a failure;
// TANDEM_REFEREE_SUITE=0 turns it off.
namespace {
struct RefereeListener : doctest::IReporter {
  explicit RefereeListener(const doctest::ContextOptions &) {}

  void test_run_start() override {
    const char *off = std::getenv("TANDEM_REFEREE_SUITE");
    if (off && std::string(off) == "0")
      return;
    tandem::Endpoint::SetCreationHook([this](tandem::Endpoint &endpoint) {
      auto referee = std::make_shared<tandem::sim::Referee>(endpoint.IsHost());
      referee->Attach(endpoint);
      m_referees.push_back(std::move(referee));
    });
  }
  void test_case_start(const doctest::TestCaseData &test) override { m_test = test.m_name; }
  void test_case_end(const doctest::CurrentTestCaseStats &) override {
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
              << " violations and " << m_hints << " hints in " << m_found.size() << " tests\n";
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

  void report_query(const doctest::QueryData &) override {}
  void test_case_reenter(const doctest::TestCaseData &) override {}
  void test_case_exception(const doctest::TestCaseException &) override {}
  void subcase_start(const doctest::SubcaseSignature &) override {}
  void subcase_end() override {}
  void log_assert(const doctest::AssertData &) override {}
  void log_message(const doctest::MessageData &) override {}
  void test_case_skipped(const doctest::TestCaseData &) override {}

  std::vector<std::shared_ptr<tandem::sim::Referee>> m_referees;
  std::string m_test;
  std::map<std::string, std::map<std::string, std::uint32_t>> m_found;
  std::vector<std::string> m_examples;
  std::size_t m_watched{};
  std::uint64_t m_violations{}, m_hints{};
};
} // namespace

REGISTER_LISTENER("referee", 1, RefereeListener);
