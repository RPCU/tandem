// The recorder (tandem/recorder.hpp): segments, their index, the size cap,
// and what a crash leaves.

#include "ports.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/play.hpp"
#include "tandem/recorder.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace tandem;

namespace {
struct Folder {
  std::filesystem::path path;
  explicit Folder(const std::string &name)
      : path(std::filesystem::temp_directory_path() /
             ("tandem-recorder-" + std::to_string(getpid()) + "-" + name)) {
    std::filesystem::remove_all(path);
  }
  ~Folder() { std::filesystem::remove_all(path); }

  std::vector<std::string> Segments() const {
    std::vector<std::string> out;
    for (const auto &entry : std::filesystem::directory_iterator(path))
      if (entry.path().extension() == ".tsession")
        out.push_back(entry.path().filename().string());
    std::ranges::sort(out);
    return out;
  }
  std::string Index() const {
    std::ifstream in(path / "index.jsonl");
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
  }
};

constexpr std::uint64_t kStart = 1'800'000'000'000'000;

proto::PlayerState At(float x) { return proto::PlayerState{.player = 1, .position = {x, 0, 0}}; }

std::vector<std::string> Lines(const std::string &text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);)
    if (!line.empty())
      out.push_back(line);
  return out;
}
} // namespace

TEST_CASE("a segment is a readable recording of everything sent, and gets its index line") {
  Folder folder("segment");
  std::vector<std::string> logs;
  record::Recorder recorder;
  REQUIRE(recorder.Start(
      {.dir = folder.path, .solo = true}, [&](const std::string &line) { logs.push_back(line); },
      [] { return std::string("{\"tick_us\":12}"); }));
  recorder.Add(kStart, At(0));
  recorder.Add(kStart + 33'000, At(1));
  recorder.Add(kStart + 66'000, proto::Shot{.player = 1});
  recorder.Add(kStart + 99'000, proto::World{.gameTimeSeconds = 3600});
  recorder.Add(kStart + 120'000, proto::NpcSpawn{.npc = 7});
  recorder.Add(kStart + 150'000, proto::PhoneCall{});
  CHECK(!recorder.Current().empty());
  recorder.Stop();

  const auto segments = folder.Segments();
  REQUIRE(segments.size() == 1);
  std::string error;
  const auto recording = play::Load(folder.path / segments[0], error);
  REQUIRE_MESSAGE(recording, error);
  CHECK(recording->header.kind == play::Kind::Session);
  CHECK(recording->header.host == 1);
  CHECK(recording->header.player == 0);
  CHECK_FALSE(recording->truncated);
  CHECK(recording->entries.size() == 6);
  CHECK(recording->header.startHostUs == kStart);
  CHECK(std::holds_alternative<proto::NpcSpawn>(recording->entries[4].message));

  const auto lines = Lines(folder.Index());
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].starts_with("{\"file\":\"" + segments[0] + "\""));
  CHECK(lines[0].find("\"tags\":[\"combat\",\"call\"]") != std::string::npos);
  CHECK(lines[0].find("\"solo\":true") != std::string::npos);
  CHECK(lines[0].find("\"metres\":1") != std::string::npos);
  CHECK(lines[0].find("\"cost\":{\"tick_us\":12}") != std::string::npos);
  CHECK(record::IndexedFiles(folder.Index()) == segments);
}

TEST_CASE("segments close when their time is up, and a cut closes one at once") {
  Folder folder("rotate");
  record::Recorder recorder;
  REQUIRE(recorder.Start({.dir = folder.path, .segmentUs = 1'000'000}, {}));
  recorder.Add(kStart, At(0));
  recorder.Tick(kStart + 500'000);
  CHECK(recorder.Segments() == 0);
  recorder.Tick(kStart + 1'000'000);
  CHECK(recorder.Segments() == 1);
  CHECK(recorder.Current().empty());
  // The next message opens the next one (another name, even within a second).
  recorder.Add(kStart + 1'100'000, At(1));
  CHECK(!recorder.Current().empty());
  recorder.Cut();
  CHECK(recorder.Segments() == 2);
  recorder.Stop();
  CHECK(folder.Segments().size() == 2);
  CHECK(Lines(folder.Index()).size() == 2);
}

TEST_CASE("the oldest segments go once the folder is over its cap") {
  Folder folder("prune");
  std::filesystem::create_directories(folder.path);
  for (const char *name : {"20261101-100000", "20261102-100000", "20261103-100000"}) {
    std::ofstream out(folder.path / (std::string(name) + ".tsession"), std::ios::binary);
    out << std::string(1000, 'x');
  }
  std::ofstream(folder.path / "notes.txt") << "not a segment";
  const auto deleted = record::Prune(folder.path, 2000, "20261101-100000.tsession");
  // The oldest is the open one: kept; the next oldest goes instead.
  CHECK(deleted == std::vector<std::string>{"20261102-100000.tsession"});
  CHECK(folder.Segments() ==
        std::vector<std::string>{"20261101-100000.tsession", "20261103-100000.tsession"});
  CHECK(std::filesystem::exists(folder.path / "notes.txt"));
}

TEST_CASE("a crash's segment is still readable, and indexed at the next start") {
  Folder folder("crash");
  {
    record::Recorder recorder;
    REQUIRE(recorder.Start({.dir = folder.path}, {}));
    for (int i = 0; i < 20; ++i)
      recorder.Add(kStart + i * 33'000, At(static_cast<float>(i)));
    recorder.Tick(kStart + 2'000'000); // flushed
    // No Stop: the game died here. (Its writer is dropped unclosed below.)
    const auto name = recorder.Current();
    REQUIRE(!name.empty());
  }
  // The file cut inside an entry, as a crash mid-write would leave it.
  const auto segments = folder.Segments();
  REQUIRE(segments.size() == 1);
  const auto path = folder.path / segments[0];
  const auto size = std::filesystem::file_size(path);
  std::filesystem::resize_file(path, size - 5);
  std::string error;
  const auto recording = play::Load(path, error);
  REQUIRE_MESSAGE(recording, error);
  CHECK(recording->truncated);
  CHECK(recording->entries.size() == 19);

  std::vector<std::string> logs;
  record::Recorder next;
  REQUIRE(next.Start({.dir = folder.path}, [&](const std::string &line) { logs.push_back(line); }));
  const auto lines = Lines(folder.Index());
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].find("\"partial\":true") != std::string::npos);
  CHECK(lines[0].find("\"messages\":19") != std::string::npos);
  next.Stop();
}

TEST_CASE("a folder it cannot make turns the recorder off, not the caller") {
  Folder folder("blocked");
  std::filesystem::create_directories(folder.path.parent_path());
  std::ofstream(folder.path) << "a file where the folder should be";
  std::vector<std::string> logs;
  record::Recorder recorder;
  CHECK_FALSE(
      recorder.Start({.dir = folder.path}, [&](const std::string &line) { logs.push_back(line); }));
  recorder.Add(kStart, At(0)); // ignored
  recorder.Tick(kStart + 1);
  recorder.Stop();
  CHECK(recorder.Failed());
  REQUIRE(logs.size() == 1);
  CHECK(logs[0].starts_with("recorder: off for this run"));
  std::filesystem::remove(folder.path);
}

TEST_CASE("the record tap hears what the host says about its V and its world, alone too") {
  std::string error;
  auto host = Endpoint::Host({"host"}, test::NextPort(), error);
  REQUIRE_MESSAGE(host, error);
  std::vector<proto::Message> heard;
  host->SetRecordTap([&](const proto::Message &message) { heard.push_back(message); });
  host->SendLocalState(At(3));
  host->SetWorld(proto::World{.gameTimeSeconds = 7200});
  host->SendNpcSpawn(proto::NpcSpawn{.npc = 3});
  host->SetLocalPaused(true);
  host->ReportDevice(proto::DeviceState{.entity = 5, .cls = 6, .flags = proto::kDeviceOpen});
  REQUIRE(heard.size() == 5);
  CHECK(std::holds_alternative<proto::PlayerState>(heard[0]));
  CHECK(std::get<proto::World>(heard[1]).gameTimeSeconds == 7200);
  CHECK(std::get<proto::NpcSpawn>(heard[2]).npc == 3);
  CHECK(std::get<proto::Pause>(heard[3]).paused);
  CHECK(std::get<proto::DeviceState>(heard[4]).entity == 5);
  host->SetRecordTap({});
  host->SendLocalState(At(4));
  CHECK(heard.size() == 5);
}
