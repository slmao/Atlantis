#include "gate_store.h"

#include "gate_set.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

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

}  // namespace atlantis::runtime::gate
