#include "gate_store.h"

#include "gate_canary.h"
#include "gate_set.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace atlantis::runtime::gate {

namespace json = atlantis::connection::json;
namespace fs = std::filesystem;
using ProfilesResult = atlantis::Result<std::vector<Profile>, std::string>;

const std::vector<std::string_view>& identityKeys() {
  static const std::vector<std::string_view> keys = {"os", "cpu", "gpu", "driver", "vulkan_api", "present_mode", "refresh_hz"};
  return keys;
}

Value identityOf(const Value& document) {
  Value identity = Value::object();
  const auto copy = [&](std::string_view key, const Value* source) {
    identity.set(std::string(key), source != nullptr ? *source : Value());
  };
  for (const std::string_view key : {"os", "cpu", "gpu", "driver", "vulkan_api"}) {
    copy(key, at(document, {"meta", "system", key}));
  }
  for (const std::string_view key : {"present_mode", "refresh_hz"}) {
    copy(key, at(document, {"meta", "display", key}));
  }
  return identity;
}

namespace {

[[nodiscard]] bool readText(const fs::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

ProfilesResult listProfiles(const std::string& baselinesDir) {
  std::vector<Profile> profiles;
  std::error_code error;
  if (!fs::is_directory(baselinesDir, error)) return ProfilesResult::Ok(std::move(profiles));

  std::vector<fs::path> directories;
  for (const auto& entry : fs::directory_iterator(baselinesDir, error)) {
    if (entry.is_directory()) directories.push_back(entry.path());
  }
  std::sort(directories.begin(), directories.end());  // a fixed order, whatever the file system returns

  for (const fs::path& directory : directories) {
    std::string text;
    const fs::path file = directory / "profile.json";
    if (!readText(file, text)) continue;
    const auto parsed = json::parse(text);
    if (parsed.isErr()) return ProfilesResult::Err(file.string() + " is not valid JSON");
    const Value& doc = parsed.value();
    const Value* schema = doc.find("schema");
    if (schema == nullptr || !schema->isString() || schema->asString() != kProfileSchema) {
      return ProfilesResult::Err(file.string() + " is not an " + std::string(kProfileSchema) + " document");
    }
    const Value* id = doc.find("id");
    const Value* label = doc.find("label");
    const Value* identity = doc.find("identity");
    if (id == nullptr || !id->isString() || id->asString().empty() || label == nullptr || !label->isString() ||
        identity == nullptr || !identity->isObject()) {
      return ProfilesResult::Err(file.string() + " lacks an id, a label or an identity");
    }
    if (id->asString() != directory.filename().string()) {
      return ProfilesResult::Err(file.string() + ": the id '" + id->asString() + "' is not the directory name");
    }
    for (const std::string_view key : identityKeys()) {
      if (identity->find(key) == nullptr) {
        return ProfilesResult::Err(file.string() + ": the identity has no '" + std::string(key) + "'");
      }
    }
    Profile profile;
    profile.id = id->asString();
    profile.label = label->asString();
    profile.identity = *identity;
    profile.directory = directory.string();
    profiles.push_back(std::move(profile));
  }
  return ProfilesResult::Ok(std::move(profiles));
}

const Profile* matchProfile(const std::vector<Profile>& profiles, const Value& identity) {
  for (const Profile& profile : profiles) {
    bool same = true;
    for (const std::string_view key : identityKeys()) {
      const Value* a = profile.identity.find(key);
      const Value* b = identity.find(key);
      if (a == nullptr || b == nullptr || !(*a == *b)) {
        same = false;
        break;
      }
    }
    if (same) return &profile;
  }
  return nullptr;
}

const Profile* findProfile(const std::vector<Profile>& profiles, std::string_view id) {
  for (const Profile& profile : profiles) {
    if (profile.id == id) return &profile;
  }
  return nullptr;
}

std::string baselinePath(const Profile& profile, std::string_view scene, std::string_view variant, unsigned int width,
                         unsigned int height) {
  const std::string name = std::string(scene) + "_" + std::string(variant) + "_" + std::to_string(width) + "x" +
                           std::to_string(height) + std::string(kBaselineSuffix);
  return (fs::path(profile.directory) / std::string(scene) / name).string();
}

LoadedBaseline loadBaseline(const std::string& path) {
  LoadedBaseline out;
  std::string text;
  if (!readText(path, text)) {
    out.notFound = true;
    return out;
  }
  auto parsed = json::parse(text);
  if (parsed.isErr()) {
    out.error = path + " is not valid JSON";
    return out;
  }
  const Value* schema = parsed.value().find("schema");
  if (schema == nullptr || !schema->isString() || schema->asString() != kSetSchema) {
    out.error = path + " is not an " + std::string(kSetSchema) + " document";
    return out;
  }
  const Value* provenance = parsed.value().find("baseline");
  if (provenance == nullptr || !provenance->isObject()) {
    out.error = path + " has no baseline provenance block (it was not recorded with --gate --record)";
    return out;
  }
  out.document = std::move(parsed.value());
  return out;
}

std::string deriveProfileId(const Value& identity) {
  std::string source;
  for (const std::string_view key : {"gpu", "os", "refresh_hz"}) {
    const Value* value = identity.find(key);
    if (value == nullptr) continue;
    if (!source.empty()) source.push_back(' ');
    source += value->isString() ? value->asString() : (value->isNumber() ? value->numberText() : std::string());
    if (key == "refresh_hz") source += "hz";
  }
  std::string id;
  bool dash = true;  // swallow leading separators
  for (const char raw : source) {
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      id.push_back(c);
      dash = false;
    } else if (!dash) {
      id.push_back('-');
      dash = true;
    }
  }
  while (!id.empty() && id.back() == '-') id.pop_back();
  return id.empty() ? std::string("unknown-profile") : id;
}

bool isValidProfileId(std::string_view id) {
  if (id.empty() || id.size() > 100) return false;
  for (const char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return id.front() != '-' && id.back() != '-';
}

Value makeBaseline(Value set, const BaselineProvenance& provenance, Value mdr) {
  Value sessions = Value::array();
  for (const std::string& start : provenance.sessionStarts) {
    Value session = Value::object();
    session.set("start", Value::string(start));
    session.set("runs", count(provenance.runsPerSession));
    sessions.push(std::move(session));
  }
  Value reason = Value::object();
  reason.set("category", Value::number(static_cast<std::int64_t>(provenance.reasonCategory)));
  reason.set("text", Value::string(provenance.reasonText));
  Value block = Value::object();
  block.set("profile", Value::string(provenance.profile));
  block.set("recorded_commit", Value::string(provenance.recordedCommit));
  block.set("git_dirty", Value::boolean(provenance.gitDirty));
  block.set("recorded_at", Value::string(provenance.recordedAt));
  // The pinning state the baseline was recorded under (mirrors protocol.pin: a pinned run never compares
  // against an unpinned baseline).
  if (const Value* pin = at(set, {"protocol", "pin"})) block.set("pin", *pin);
  // The power source it was recorded on (mirrors protocol.power: a mains baseline is never compared with anything else).
  if (const Value* power = at(set, {"protocol", "power"})) block.set("power", *power);
  block.set("sessions", std::move(sessions));
  block.set("reason", std::move(reason));
  set.set("mdr", std::move(mdr));
  set.set("baseline", std::move(block));
  return set;
}

std::vector<std::string> validateStore(const std::string& baselinesDir, const RecordPolicy* bar) {
  std::vector<std::string> problems;
  const auto profiles = listProfiles(baselinesDir);
  if (profiles.isErr()) {
    problems.push_back(profiles.error());
    return problems;
  }
  for (const Profile& profile : profiles.value()) {
    std::error_code error;
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(profile.directory, error)) {
      if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
      const std::string name = file.filename().string();
      const std::string where = file.string();
      if (file.parent_path() == fs::path(profile.directory) && name == "profile.json") continue;
      if (name.size() <= kBaselineSuffix.size() ||
          name.compare(name.size() - kBaselineSuffix.size(), kBaselineSuffix.size(), kBaselineSuffix) != 0) {
        problems.push_back(where + ": not a baseline file (a profile directory holds profile.json and *" +
                           std::string(kBaselineSuffix) + " only)");
        continue;
      }
      const LoadedBaseline loaded = loadBaseline(where);
      if (!loaded.error.empty()) {
        problems.push_back(loaded.error);
        continue;
      }
      const Value& doc = loaded.document;
      const Value* block = doc.find("baseline");
      const auto field = [&](std::initializer_list<std::string_view> path) { return at(*block, path); };
      if (const Value* p = field({"profile"}); p == nullptr || !p->isString() || p->asString() != profile.id) {
        problems.push_back(where + ": baseline.profile is not '" + profile.id + "'");
      }
      if (const Value* c = field({"recorded_commit"}); c == nullptr || !c->isString()) {
        problems.push_back(where + ": baseline.recorded_commit is missing");
      }
      if (const Value* s = field({"sessions"}); s == nullptr || !s->isArray() || s->asArray().empty()) {
        problems.push_back(where + ": baseline.sessions is missing or empty");
      }
      const auto category = numberOf(field({"reason", "category"}));
      if (!category || *category < 1.0 || *category > 4.0) {
        problems.push_back(where + ": baseline.reason.category is not 1, 2, 3 or 4 (ADR-0118 D3)");
      }
      if (const Value* t = field({"reason", "text"}); t == nullptr || !t->isString() || t->asString().empty()) {
        problems.push_back(where + ": baseline.reason.text is empty (a baseline is only recorded with a stated reason)");
      }
      const Value identity = identityOf(doc);
      bool same = true;
      for (const std::string_view key : identityKeys()) {
        const Value* a = identity.find(key);
        const Value* b = profile.identity.find(key);
        if (a == nullptr || b == nullptr || !(*a == *b)) same = false;
      }
      if (!same) problems.push_back(where + ": its meta is not the identity of profile '" + profile.id + "'");
      const Value* pin = at(doc, {"protocol", "pin"});
      const Value* recordedPin = field({"pin"});
      if (pin == nullptr || !pin->isObject() || recordedPin == nullptr || !(*recordedPin == *pin)) {
        problems.push_back(where + ": protocol.pin and baseline.pin are missing or differ (the pinning state is part of a baseline)");
      }
      if (bar != nullptr) {
        const double spread = canarySpreadPct(doc);
        if (spread < 0.0) {
          problems.push_back(where + ": it has no GPU canary reference (record it again with a build that has the canary)");
        } else if (spread > bar->maxCanarySpreadPct) {
          problems.push_back(where + ": its GPU canary readings spread " + formatNumber(std::round(spread * 10.0) / 10.0) +
                             " % across the recording, above the record bar of " + formatNumber(bar->maxCanarySpreadPct) +
                             " % (a recording the bar would have refused)");
        }
        for (const auto& [tier, bound] : {std::pair<std::string_view, double>{"cpu", bar->maxMdrCpuPct},
                                          std::pair<std::string_view, double>{"gpu", bar->maxMdrGpuPct}}) {
          const auto mdr = numberOf(at(doc, {"mdr", tier}));
          if (mdr && *mdr > bound) {
            problems.push_back(where + ": its " + std::string(tier) + " minimum detectable regression " +
                               formatNumber(std::round(*mdr * 10.0) / 10.0) + " % is above the record bar of " +
                               formatNumber(bound) + " % (a recording the bar would have refused)");
          }
        }
      }
      const Value* power = at(doc, {"protocol", "power"});
      const Value* recordedPower = field({"power"});
      if (power == nullptr || !power->isString() || recordedPower == nullptr || !(*recordedPower == *power) ||
          power->asString() != "mains") {
        problems.push_back(where + ": protocol.power and baseline.power must both be 'mains' (a baseline is only recorded on mains power)");
      }
      const Value* idle = at(doc, {"protocol", "idle_wait"});
      const auto preheat = numberOf(at(doc, {"protocol", "preheat_seconds"}));
      if (idle == nullptr || !idle->isBool() || !idle->asBool() || !preheat || *preheat <= 0.0) {
        problems.push_back(where + ": it was not recorded under the full protocol (idle wait and a preheat)");
      }
      const Value* aggregate = doc.find("aggregate");
      if (aggregate == nullptr || !aggregate->isObject() || aggregate->asObject().empty()) {
        problems.push_back(where + ": it has no aggregate");
      }
      const Value* scene = at(doc, {"scene", "name"});
      const Value* variant = at(doc, {"settings", "variant"});
      const auto width = numberOf(at(doc, {"settings", "resolution", "width"}));
      const auto height = numberOf(at(doc, {"settings", "resolution", "height"}));
      if (scene == nullptr || !scene->isString() || variant == nullptr || !variant->isString() || !width || !height) {
        problems.push_back(where + ": its scene, variant or resolution is missing");
      } else {
        const std::string expected = baselinePath(profile, scene->asString(), variant->asString(),
                                                  static_cast<unsigned int>(*width), static_cast<unsigned int>(*height));
        std::error_code ignored;
        if (!fs::equivalent(fs::path(expected), file, ignored)) {
          problems.push_back(where + ": it belongs at " + expected + " (the store layout)");
        }
      }
    }
  }
  return problems;
}

atlantis::Result<WrittenFiles, std::string> writeCandidate(const std::string& recordTo, const Profile& profile,
                                                           const Value& baseline, std::string_view scene,
                                                           std::string_view variant, unsigned int width,
                                                           unsigned int height, std::string_view label) {
  using Out = atlantis::Result<WrittenFiles, std::string>;
  if (recordTo.empty()) return Out::Err("no destination was named");
  if (!isValidProfileId(profile.id)) return Out::Err("'" + profile.id + "' is not a valid profile id (a-z, 0-9, '-')");
  WrittenFiles files;
  std::error_code error;
  const fs::path profileDir = fs::path(recordTo) / profile.id;
  fs::create_directories(profileDir / std::string(scene), error);
  if (error) return Out::Err("cannot create " + profileDir.string() + ": " + error.message());

  const fs::path profileFile = profileDir / "profile.json";
  std::string existing;
  if (readText(profileFile, existing)) {
    const auto parsed = json::parse(existing);
    if (parsed.isErr()) return Out::Err(profileFile.string() + " exists and is not valid JSON");
    const Value* identity = parsed.value().find("identity");
    if (identity == nullptr || !(*identity == profile.identity)) {
      return Out::Err(profileFile.string() +
                      " describes different hardware; choose another --profile id (a profile's identity is never rewritten)");
    }
  } else {
    Value doc = Value::object();
    doc.set("schema", Value::string(std::string(kProfileSchema)));
    doc.set("id", Value::string(profile.id));
    doc.set("label", Value::string(std::string(label)));
    doc.set("identity", profile.identity);
    std::ofstream out(profileFile, std::ios::binary | std::ios::trunc);
    if (!out) return Out::Err("cannot write " + profileFile.string());
    out << json::write(doc) << "\n";
    files.profileCreated = true;
  }
  files.profileFile = profileFile.string();

  Profile where = profile;
  where.directory = profileDir.string();
  const std::string baselineFile = baselinePath(where, scene, variant, width, height);
  std::ofstream out(baselineFile, std::ios::binary | std::ios::trunc);
  if (!out) return Out::Err("cannot write " + baselineFile);
  out << json::write(baseline) << "\n";
  files.baselineFile = baselineFile;
  return Out::Ok(std::move(files));
}

}  // namespace atlantis::runtime::gate
