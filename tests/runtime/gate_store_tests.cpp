#include "../../src/runtime/gate_store.h"

#include "../../src/runtime/gate_evaluate.h"
#include "../../src/runtime/gate_set.h"
#include "gate_test_support.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

// Spec 0060 R3, R4 / ADR-0118 D2-D3, ADR-0119 D4, D6 (Plan 0060 M5-M6): the baseline store -- the
// profile identity, the layout, the provenance a recorded baseline carries, the writer that
// writes only where it is told, the guard over the committed tree, and the minimum detectable
// regression a baseline states. Synthetic documents, a temporary directory, no device.

namespace {

namespace gate = atlantis::runtime::gate;
namespace json = atlantis::connection::json;
namespace fs = std::filesystem;
using Catch::Approx;
using gate_test::DocumentIdentity;
using gate_test::makeRun;

struct TempStore {
  TempStore() {
    root = gate_test::uniqueTempDir("atlantis_gate_store_test");
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~TempStore() { fs::remove_all(root); }
  [[nodiscard]] std::string dir() const { return root.string(); }
  fs::path root;
};

json::Value setOf(const DocumentIdentity& identity = {}, bool idleWait = true, double preheat = 90.0) {
  std::vector<json::Value> runs;
  for (const double v : {12.0, 12.1, 11.9, 12.0, 12.05}) {
    runs.push_back(makeRun({{"cpu_active_ms", v}, {"gpu_busy_ms", v + 5.0}, {"draw_calls", 4778.0}}, identity));
  }
  gate::Protocol protocol;
  protocol.idleWait = idleWait;
  protocol.preheatSeconds = preheat;
  protocol.pin = true;
  protocol.power = "mains";
  const auto set = gate::buildSet(protocol, runs);
  REQUIRE(set.isOk());
  return set.value();
}

gate::BaselineProvenance provenance(const std::string& profile = "test-profile") {
  gate::BaselineProvenance p;
  p.profile = profile;
  p.recordedCommit = "abc123def456";
  p.recordedAt = "2026-10-12T01:02:03Z";
  p.sessionStarts = {"2026-10-12T00:00:00Z", "2026-10-12T00:10:00Z", "2026-10-12T00:20:00Z"};
  p.runsPerSession = 5;
  p.reasonCategory = 4;
  p.reasonText = "initial baseline for the test profile";
  return p;
}

gate::Profile profileOf(const json::Value& set, const std::string& id = "test-profile") {
  gate::Profile profile;
  profile.id = id;
  profile.identity = gate::identityOf(set);
  return profile;
}

// Writes a valid store with one profile and one baseline; returns the baseline's path.
std::string writeValidStore(const TempStore& store, const DocumentIdentity& identity = {}) {
  const json::Value set = setOf(identity);
  const json::Value baseline = gate::makeBaseline(set, provenance(), json::Value::object());
  const auto written = gate::writeCandidate(store.dir(), profileOf(set), baseline, "bistro", "baseline", 1920, 1080,
                                            "Test machine");
  REQUIRE(written.isOk());
  return written.value().baselineFile;
}

std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("a machine's identity is the seven hardware members of a document's meta", "[runtime][gate][store]") {
  const json::Value identity = gate::identityOf(setOf());
  REQUIRE(identity.isObject());
  CHECK(identity.asObject().size() == 7);
  CHECK(identity.find("os")->asString() == "windows");
  CHECK(identity.find("cpu")->asString() == "Test CPU");
  CHECK(identity.find("gpu")->asString() == "Test GPU");
  CHECK(identity.find("driver")->asString() == "1.0");
  CHECK(identity.find("vulkan_api")->asString() == "1.4.0");
  CHECK(identity.find("present_mode")->asString() == "FIFO");
  CHECK(*gate::numberOf(identity.find("refresh_hz")) == 60.0);
  CHECK(identity == gate::identityOf(makeRun({{"cpu_active_ms", 1.0}})));  // a benchmark document gives the same
}

TEST_CASE("an absent or empty store has no profiles and no problems", "[runtime][gate][store]") {
  TempStore store;
  const auto absent = gate::listProfiles((store.root / "nowhere").string());
  REQUIRE(absent.isOk());
  CHECK(absent.value().empty());
  const auto empty = gate::listProfiles(store.dir());
  REQUIRE(empty.isOk());
  CHECK(empty.value().empty());
  CHECK(gate::validateStore(store.dir()).empty());
  CHECK(gate::validateStore((store.root / "nowhere").string()).empty());
}

TEST_CASE("a written profile is listed in a fixed order and matched by its exact identity", "[runtime][gate][store]") {
  TempStore store;
  writeValidStore(store);
  DocumentIdentity other;
  other.gpu = "Another GPU";
  const json::Value otherSet = setOf(other);
  const auto written = gate::writeCandidate(store.dir(), profileOf(otherSet, "another-profile"),
                                            gate::makeBaseline(otherSet, provenance("another-profile"), json::Value::object()),
                                            "bistro", "baseline", 1920, 1080, "Another machine");
  REQUIRE(written.isOk());

  const auto listed = gate::listProfiles(store.dir());
  REQUIRE(listed.isOk());
  REQUIRE(listed.value().size() == 2);
  CHECK(listed.value()[0].id == "another-profile");  // sorted by directory name
  CHECK(listed.value()[1].id == "test-profile");
  CHECK(listed.value()[1].label == "Test machine");

  const gate::Profile* match = gate::matchProfile(listed.value(), gate::identityOf(setOf()));
  REQUIRE(match != nullptr);
  CHECK(match->id == "test-profile");
  CHECK(gate::findProfile(listed.value(), "another-profile") != nullptr);
  CHECK(gate::findProfile(listed.value(), "missing") == nullptr);
}

TEST_CASE("a single differing member makes a machine a different profile", "[runtime][gate][store]") {
  TempStore store;
  writeValidStore(store);
  const auto listed = gate::listProfiles(store.dir());
  REQUIRE(listed.isOk());
  const struct {
    const char* member;
    void (*change)(DocumentIdentity&);
  } cases[] = {
      {"os", [](DocumentIdentity& d) { d.os = "android"; }},
      {"cpu", [](DocumentIdentity& d) { d.cpu = "Faster CPU"; }},
      {"gpu", [](DocumentIdentity& d) { d.gpu = "Faster GPU"; }},
      {"driver", [](DocumentIdentity& d) { d.driver = "1.1"; }},
      {"vulkan_api", [](DocumentIdentity& d) { d.vulkanApi = "1.3.0"; }},
      {"present_mode", [](DocumentIdentity& d) { d.presentMode = "MAILBOX"; }},
      {"refresh_hz", [](DocumentIdentity& d) { d.refreshHz = 144; }},
  };
  for (const auto& c : cases) {
    INFO(c.member);
    DocumentIdentity id;
    c.change(id);
    CHECK(gate::matchProfile(listed.value(), gate::identityOf(setOf(id))) == nullptr);
  }
}

TEST_CASE("a malformed profile file is an error, naming the file", "[runtime][gate][store]") {
  const struct {
    const char* body;
    const char* mention;
  } cases[] = {
      {"{not json", "not valid JSON"},
      {R"({"schema":"something/1","id":"p","label":"l","identity":{}})", "is not an atlantis.benchmark.profile/1"},
      {R"({"schema":"atlantis.benchmark.profile/1","id":"other","label":"l","identity":{"os":"w"}})", "is not the directory name"},
      {R"({"schema":"atlantis.benchmark.profile/1","id":"p","label":"l"})", "lacks an id, a label or an identity"},
      {R"({"schema":"atlantis.benchmark.profile/1","id":"p","label":"l","identity":{"os":"w"}})", "has no 'cpu'"},
  };
  for (const auto& c : cases) {
    INFO(c.body);
    TempStore store;
    fs::create_directories(store.root / "p");
    std::ofstream(store.root / "p" / "profile.json", std::ios::binary) << c.body;
    const auto listed = gate::listProfiles(store.dir());
    REQUIRE(listed.isErr());
    CHECK(listed.error().find(c.mention) != std::string::npos);
    CHECK(listed.error().find("profile.json") != std::string::npos);
  }
  TempStore store;
  fs::create_directories(store.root / "no-profile-file");  // a directory without profile.json is skipped
  CHECK(gate::listProfiles(store.dir()).value().empty());
}

TEST_CASE("a baseline's path follows the store layout", "[runtime][gate][store]") {
  gate::Profile profile;
  profile.id = "p";
  profile.directory = "store/p";
  const std::string path = gate::baselinePath(profile, "bistro", "no-fog", 1920, 1080);
  CHECK(fs::path(path) == fs::path("store/p/bistro/bistro_no-fog_1920x1080.baseline.json"));
}

TEST_CASE("a baseline loads only if it is a set with provenance", "[runtime][gate][store]") {
  TempStore store;
  const std::string good = writeValidStore(store);
  const auto loaded = gate::loadBaseline(good);
  CHECK(loaded.error.empty());
  CHECK_FALSE(loaded.notFound);
  CHECK(loaded.document.find("schema")->asString() == "atlantis.benchmark.set/1");

  CHECK(gate::loadBaseline((store.root / "nothing.json").string()).notFound);

  const fs::path bad = store.root / "bad.json";
  std::ofstream(bad, std::ios::binary) << "{x";
  CHECK(gate::loadBaseline(bad.string()).error.find("not valid JSON") != std::string::npos);
  std::ofstream(bad, std::ios::binary | std::ios::trunc) << R"({"schema":"atlantis.benchmark/1"})";
  CHECK(gate::loadBaseline(bad.string()).error.find("not an atlantis.benchmark.set/1") != std::string::npos);
  std::ofstream(bad, std::ios::binary | std::ios::trunc) << json::write(setOf());
  CHECK(gate::loadBaseline(bad.string()).error.find("no baseline provenance") != std::string::npos);
}

TEST_CASE("a profile id is derived from the machine and checked for shape", "[runtime][gate][store]") {
  json::Value identity = json::Value::object();
  identity.set("gpu", json::Value::string("Intel(R) Arc(TM) B370 GPU"));
  identity.set("os", json::Value::string("windows"));
  identity.set("refresh_hz", json::Value::number(std::uint64_t{60}));
  CHECK(gate::deriveProfileId(identity) == "intel-r-arc-tm-b370-gpu-windows-60hz");
  CHECK(gate::deriveProfileId(json::Value::object()) == "unknown-profile");
  CHECK(gate::isValidProfileId("intel-arc-b370"));
  CHECK(gate::isValidProfileId("a"));
  for (const char* bad : {"", "-a", "a-", "A", "a b", "a_b", "a/b", "..", "a.b"}) {
    INFO(bad);
    CHECK_FALSE(gate::isValidProfileId(bad));
  }
  CHECK_FALSE(gate::isValidProfileId(std::string(101, 'a')));
}

TEST_CASE("a recorded baseline carries its provenance in a fixed order", "[runtime][gate][store]") {
  const json::Value baseline = gate::makeBaseline(setOf(), provenance(), json::Value::object());
  const json::Value* block = baseline.find("baseline");
  REQUIRE(block != nullptr);
  CHECK(block->find("profile")->asString() == "test-profile");
  CHECK(block->find("recorded_commit")->asString() == "abc123def456");
  CHECK_FALSE(block->find("git_dirty")->asBool());
  CHECK(block->find("recorded_at")->asString() == "2026-10-12T01:02:03Z");
  REQUIRE(block->find("sessions")->asArray().size() == 3);
  CHECK(block->find("sessions")->asArray()[1].find("start")->asString() == "2026-10-12T00:10:00Z");
  CHECK(*gate::numberOf(block->find("sessions")->asArray()[1].find("runs")) == 5.0);
  CHECK(*gate::numberOf(gate::at(baseline, {"baseline", "reason", "category"})) == 4.0);
  CHECK(gate::at(baseline, {"baseline", "reason", "text"})->asString() == "initial baseline for the test profile");
  CHECK(baseline.find("mdr") != nullptr);
  CHECK(baseline.find("aggregate") != nullptr);  // the set is intact
  const auto parsed = json::parse(json::write(baseline));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value() == baseline);
}

TEST_CASE("the writer writes the profile and the baseline where it is told, and reads back", "[runtime][gate][store]") {
  TempStore store;
  const std::string path = writeValidStore(store);
  CHECK(fs::path(path) == store.root / "test-profile" / "bistro" / "bistro_baseline_1920x1080.baseline.json");
  CHECK(fs::exists(store.root / "test-profile" / "profile.json"));
  const auto listed = gate::listProfiles(store.dir());
  REQUIRE(listed.isOk());
  REQUIRE(listed.value().size() == 1);
  CHECK(gate::loadBaseline(path).error.empty());
  CHECK(gate::validateStore(store.dir()).empty());
}

TEST_CASE("an existing profile of the same identity is left as it is", "[runtime][gate][store]") {
  TempStore store;
  writeValidStore(store);
  const std::string before = readAll((store.root / "test-profile" / "profile.json").string());
  const json::Value set = setOf();
  const auto again = gate::writeCandidate(store.dir(), profileOf(set), gate::makeBaseline(set, provenance(), json::Value::object()),
                                          "bistro", "baseline", 1920, 1080, "A different label");
  REQUIRE(again.isOk());
  CHECK_FALSE(again.value().profileCreated);
  CHECK(readAll((store.root / "test-profile" / "profile.json").string()) == before);  // never rewritten
}

TEST_CASE("a profile id that already describes other hardware is refused and nothing changes", "[runtime][gate][store]") {
  TempStore store;
  const std::string baselineFile = writeValidStore(store);
  const std::string profileBefore = readAll((store.root / "test-profile" / "profile.json").string());
  const std::string baselineBefore = readAll(baselineFile);
  DocumentIdentity other;
  other.gpu = "Another GPU";
  const json::Value set = setOf(other);
  const auto refused = gate::writeCandidate(store.dir(), profileOf(set, "test-profile"),
                                            gate::makeBaseline(set, provenance(), json::Value::object()), "bistro",
                                            "baseline", 1920, 1080, "x");
  REQUIRE(refused.isErr());
  CHECK(refused.error().find("different hardware") != std::string::npos);
  CHECK(readAll((store.root / "test-profile" / "profile.json").string()) == profileBefore);
  CHECK(readAll(baselineFile) == baselineBefore);
}

TEST_CASE("the writer refuses a bad destination or profile id", "[runtime][gate][store]") {
  TempStore store;
  const json::Value set = setOf();
  const json::Value baseline = gate::makeBaseline(set, provenance(), json::Value::object());
  CHECK(gate::writeCandidate("", profileOf(set), baseline, "bistro", "baseline", 1920, 1080, "x").isErr());
  CHECK(gate::writeCandidate(store.dir(), profileOf(set, "Bad Id"), baseline, "bistro", "baseline", 1920, 1080, "x").isErr());
  CHECK(fs::is_empty(store.root));
}

TEST_CASE("the guard finds every way a committed baseline can be wrong", "[runtime][gate][store]") {
  const auto problemsFor = [](const std::function<void(const TempStore&, const std::string&)>& damage) {
    TempStore store;
    const std::string baselineFile = writeValidStore(store);
    REQUIRE(gate::validateStore(store.dir()).empty());
    damage(store, baselineFile);
    return gate::validateStore(store.dir());
  };
  const auto mentions = [](const std::vector<std::string>& problems, const std::string& text) {
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(text) != std::string::npos; });
  };
  const auto rewrite = [](const std::string& path, const std::function<void(json::Value&)>& change) {
    auto parsed = json::parse(readAll(path));
    REQUIRE(parsed.isOk());
    json::Value doc = parsed.value();
    change(doc);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << json::write(doc) << "\n";
  };

  // No reason text, a category outside 1-4, no sessions, a profile name that is not the directory's.
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) {
                     json::Value reason = json::Value::object();
                     reason.set("category", json::Value::number(std::int64_t{4}));
                     reason.set("text", json::Value::string(""));
                     json::Value block = *d.find("baseline");
                     block.set("reason", std::move(reason));
                     d.set("baseline", std::move(block));
                   });
                 }),
                 "reason.text is empty"));
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) {
                     json::Value block = *d.find("baseline");
                     json::Value reason = *block.find("reason");
                     reason.set("category", json::Value::number(std::int64_t{7}));
                     block.set("reason", std::move(reason));
                     d.set("baseline", std::move(block));
                   });
                 }),
                 "reason.category"));
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) {
                     json::Value block = *d.find("baseline");
                     block.set("sessions", json::Value::array());
                     d.set("baseline", std::move(block));
                   });
                 }),
                 "baseline.sessions"));
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) {
                     json::Value block = *d.find("baseline");
                     block.set("profile", json::Value::string("somebody-else"));
                     d.set("baseline", std::move(block));
                   });
                 }),
                 "baseline.profile"));
  // The meta of another machine, a smoke-run protocol, no aggregate.
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) {
                     json::Value meta = *d.find("meta");
                     json::Value system = *meta.find("system");
                     system.set("gpu", json::Value::string("Not The Profile GPU"));
                     meta.set("system", std::move(system));
                     d.set("meta", std::move(meta));
                   });
                 }),
                 "its meta is not the identity"));
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) {
                     json::Value protocol = *d.find("protocol");
                     protocol.set("idle_wait", json::Value::boolean(false));
                     d.set("protocol", std::move(protocol));
                   });
                 }),
                 "full protocol"));
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) { d.set("aggregate", json::Value::object()); });
                 }),
                 "no aggregate"));
  // The pinning state is part of a baseline: missing, or not mirrored in the provenance.
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) {
                     json::Value block = *d.find("baseline");
                     block.set("pin", gate::pinValue(false));
                     d.set("baseline", std::move(block));
                   });
                 }),
                 "protocol.pin and baseline.pin"));
  // The power source too: a baseline is only ever recorded on mains power, and the provenance mirrors it.
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) {
                     json::Value block = *d.find("baseline");
                     block.set("power", json::Value::string("battery"));
                     d.set("baseline", std::move(block));
                   });
                 }),
                 "protocol.power and baseline.power"));
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) {
                     json::Value protocol = *d.find("protocol");
                     protocol.set("power", json::Value::string("battery"));
                     d.set("protocol", std::move(protocol));
                     json::Value block = *d.find("baseline");
                     block.set("power", json::Value::string("battery"));
                     d.set("baseline", std::move(block));
                   });
                 }),
                 "only recorded on mains power"));
  // A baseline in the wrong place, and a stray file.
  CHECK(mentions(problemsFor([&](const TempStore& s, const std::string& f) {
                   fs::rename(f, s.root / "test-profile" / "bistro" / "wrong_name.baseline.json");
                 }),
                 "the store layout"));
  CHECK(mentions(problemsFor([&](const TempStore& s, const std::string&) {
                   std::ofstream(s.root / "test-profile" / "notes.txt") << "hi";
                 }),
                 "not a baseline file"));
  // A baseline without provenance is not a baseline.
  CHECK(mentions(problemsFor([&](const TempStore&, const std::string& f) {
                   rewrite(f, [](json::Value& d) {
                     json::Value stripped = json::Value::object();
                     for (const auto& [key, value] : d.asObject()) {
                       if (key != "baseline") stripped.set(key, value);
                     }
                     d = stripped;
                   });
                 }),
                 "no baseline provenance"));
}

TEST_CASE("the committed baseline store passes its own guard", "[runtime][gate][store]") {
  const auto policy = gate::loadPolicy(std::string(ATLANTIS_PERFORMANCE_DIR) + "/gate_policy.json");
  REQUIRE(policy.isOk());
  // With the record bar: a committed baseline is one the bar would have accepted.
  const auto problems = gate::validateStore(std::string(ATLANTIS_PERFORMANCE_DIR) + "/baselines", &policy.value().record);
  for (const std::string& problem : problems) UNSCOPED_INFO(problem);
  CHECK(problems.empty());  // vacuously true until a baseline is committed
}

TEST_CASE("the minimum detectable regression follows from the baseline's noise band", "[runtime][gate][store]") {
  gate::GatePolicy policy;
  policy.protocol.k = 3.0;
  policy.cpu.absFloor = 0.10;
  policy.cpu.maxPct = 5.0;
  policy.gpu.absFloor = 0.25;
  policy.gpu.maxPct = 10.0;
  const json::Value set = gate_test::makeSet({{"cpu_active_ms", {12.0, 0.1, 15.0}}, {"gpu_busy_ms", {17.0, 0.6, 15.0}}});
  const json::Value mdr = gate::computeMdr(set, policy, 5);
  CHECK(*gate::numberOf(mdr.find("counter")) == 0.0);
  // cpu: tolerance max(0.1, 0.6, 3*0.1*1.25*sqrt(1/15+1/5)=0.1936) = 0.6; + 1.2816 * 1.2533*0.1/sqrt(5) = 0.0718
  // -> 0.6718 / 12 = 5.598 %.
  CHECK(*gate::numberOf(mdr.find("cpu")) == Approx(5.598).margin(0.01));
  // gpu: tolerance max(0.25, 1.7, 0.7746) = 1.7; + 1.2816 * 1.2533*0.6/sqrt(5) = 0.431 -> 2.131 / 17 = 12.54 %.
  CHECK(*gate::numberOf(mdr.find("gpu")) == Approx(12.54).margin(0.02));
  // A noisy baseline raises the resolution figure; a tier with no metric to read is null.
  const json::Value noisy = gate_test::makeSet({{"cpu_active_ms", {12.0, 1.0, 15.0}}});
  CHECK(*gate::numberOf(gate::computeMdr(noisy, policy, 5).find("cpu")) > 15.0);
  CHECK(gate::computeMdr(noisy, policy, 5).find("gpu")->isNull());
}

TEST_CASE("the guard, given the record bar, refuses a committed baseline the bar would have refused",
          "[runtime][gate][store][bar]") {
  TempStore store;
  const std::string baselineFile = writeValidStore(store);
  const gate::RecordPolicy bar{10.0, 20.0};
  const auto setMdr = [&](double cpu, double gpu) {
    auto parsed = json::parse(readAll(baselineFile));
    REQUIRE(parsed.isOk());
    json::Value doc = parsed.value();
    json::Value mdr = json::Value::object();
    mdr.set("counter", gate::num(0.0));
    mdr.set("cpu", gate::num(cpu));
    mdr.set("gpu", gate::num(gpu));
    doc.set("mdr", std::move(mdr));
    doc.set("canary", gate_test::canaryOf({gate_test::kStableCanary, gate_test::kStableCanary, gate_test::kStableCanary,
                                           gate_test::kStableCanary}));
    std::ofstream out(baselineFile, std::ios::binary | std::ios::trunc);
    out << json::write(doc) << "\n";
  };
  const auto mentions = [](const std::vector<std::string>& problems, const std::string& text) {
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(text) != std::string::npos; });
  };
  setMdr(6.5, 17.2);  // what the calibration study could resolve
  CHECK(gate::validateStore(store.dir(), &bar).empty());
  setMdr(18.2, 11.6);  // the M10 recording that motivated the bar
  CHECK(mentions(gate::validateStore(store.dir(), &bar), "cpu minimum detectable regression 18.2 %"));
  CHECK(gate::validateStore(store.dir()).empty());  // without a bar the guard is as before
  setMdr(6.5, 21.5);  // the recording that spanned battery and mains
  CHECK(mentions(gate::validateStore(store.dir(), &bar), "gpu minimum detectable regression 21.5 %"));
  setMdr(10.0, 20.0);  // exactly at the bound is inside it
  CHECK(gate::validateStore(store.dir(), &bar).empty());
}

TEST_CASE("the guard, given the record bar, wants a GPU canary reference whose readings agree",
          "[runtime][gate][store][canary]") {
  TempStore store;
  const std::string baselineFile = writeValidStore(store);
  const gate::RecordPolicy bar{10.0, 20.0, 5.0};
  const auto setCanary = [&](const std::vector<gate_test::CanaryMs>& readings) {
    auto parsed = json::parse(readAll(baselineFile));
    REQUIRE(parsed.isOk());
    json::Value doc = parsed.value();
    if (readings.empty()) {
      json::Value rebuilt = json::Value::object();
      for (const auto& [key, value] : doc.asObject()) {
        if (key != "canary") rebuilt.set(key, value);
      }
      doc = std::move(rebuilt);
    } else {
      doc.set("canary", gate_test::canaryOf(readings));
    }
    std::ofstream out(baselineFile, std::ios::binary | std::ios::trunc);
    out << json::write(doc) << "\n";
  };
  const auto mentions = [](const std::vector<std::string>& problems, const std::string& text) {
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(text) != std::string::npos; });
  };
  const gate_test::CanaryMs high = {gate_test::kStableCanary[0], gate_test::kStableCanary[1] * 1.12,
                                    gate_test::kStableCanary[2]};
  const gate_test::CanaryMs slightlyHigh = {gate_test::kStableCanary[0], gate_test::kStableCanary[1] * 1.04,
                                            gate_test::kStableCanary[2]};

  setCanary({});
  CHECK(mentions(gate::validateStore(store.dir(), &bar), "no GPU canary reference"));
  CHECK(gate::validateStore(store.dir()).empty());  // without a bar the guard is as before

  setCanary({gate_test::kStableCanary, gate_test::kStableCanary, gate_test::kStableCanary, gate_test::kStableCanary});
  CHECK(gate::validateStore(store.dir(), &bar).empty());

  setCanary({gate_test::kStableCanary, gate_test::kStableCanary, high, high});  // two GPU states in one recording
  CHECK(mentions(gate::validateStore(store.dir(), &bar), "GPU canary readings spread 12 %"));

  setCanary({gate_test::kStableCanary, slightlyHigh});  // 4 %: inside the bar
  CHECK(gate::validateStore(store.dir(), &bar).empty());
}
