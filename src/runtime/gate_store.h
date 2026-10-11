#pragma once

// Spec 0060 / ADR-0118 D2, ADR-0119 D4 (Plan 0060 M5-M6, P4): the committed baseline store.
//
//   <baselines>/<profile-id>/profile.json                          the hardware identity
//   <baselines>/<profile-id>/<scene>/<scene>_<variant>_<W>x<H>.baseline.json
//
// A hardware profile is the exact equality of the machine's `meta.system.{os, cpu, gpu,
// driver, vulkan_api}` and `meta.display.{present_mode, refresh_hz}` (Spec 0060 Q5). This file
// reads the store and, for recording, writes a candidate baseline to a path the caller names; it
// never writes into the committed store on its own (ADR-0118 D3).
//
// Private to the atlantis_runtime executable's startup layer (see gate_set.h); GPU-independent.

#include "gate_json.h"

#include <atlantis/result.h>

#include <string>
#include <string_view>
#include <vector>

namespace atlantis::runtime::gate {

inline constexpr std::string_view kProfileSchema = "atlantis.benchmark.profile/1";
inline constexpr std::string_view kBaselineSuffix = ".baseline.json";

struct Profile {
  std::string id;         // the directory name and the slug
  std::string label;      // for people
  Value identity;         // {os, cpu, gpu, driver, vulkan_api, present_mode, refresh_hz}
  std::string directory;  // <baselines>/<id>
};

// The identity members, in a fixed order, as they appear in `profile.json`.
[[nodiscard]] const std::vector<std::string_view>& identityKeys();

// This machine's identity, taken from a set or benchmark document's `meta`.
[[nodiscard]] Value identityOf(const Value& document);

// Every profile directory under `baselinesDir` (a directory without profile.json is skipped; a
// malformed profile.json is an error). A missing `baselinesDir` is an empty store, not an error.
[[nodiscard]] atlantis::Result<std::vector<Profile>, std::string> listProfiles(const std::string& baselinesDir);

// The profile whose identity equals `identity` exactly, if any.
[[nodiscard]] const Profile* matchProfile(const std::vector<Profile>& profiles, const Value& identity);
[[nodiscard]] const Profile* findProfile(const std::vector<Profile>& profiles, std::string_view id);

// `<profile.directory>/<scene>/<scene>_<variant>_<W>x<H>.baseline.json`.
[[nodiscard]] std::string baselinePath(const Profile& profile, std::string_view scene, std::string_view variant,
                                       unsigned int width, unsigned int height);

// The baseline at `path`: an `atlantis.benchmark.set/1` document with a `baseline` provenance
// block. Not found is `notFound` true (the caller says "record one"); anything else wrong is an error.
struct LoadedBaseline {
  bool notFound = false;
  Value document;
  std::string error;
};
[[nodiscard]] LoadedBaseline loadBaseline(const std::string& path);

}  // namespace atlantis::runtime::gate
