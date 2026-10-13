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
#include "gate_policy.h"

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

// A profile id for a machine identity: lowercase letters, digits and '-', e.g.
// `intel-r-arc-tm-b370-gpu-windows-60hz`. A person may choose a nicer one with --profile.
[[nodiscard]] std::string deriveProfileId(const Value& identity);
[[nodiscard]] bool isValidProfileId(std::string_view id);

// The provenance a recorded baseline carries (ADR-0118 D3): where and when it was recorded, from
// which commit, over how many sessions, and the stated reason.
struct BaselineProvenance {
  std::string profile;
  std::string recordedCommit;
  bool gitDirty = false;
  std::string recordedAt;                       // YYYY-MM-DDTHH:MM:SSZ
  std::vector<std::string> sessionStarts;       // one per session
  unsigned int runsPerSession = 0;
  unsigned int reasonCategory = 0;              // 1..4
  std::string reasonText;
};

// `set` with its `baseline` block (profile, recorded_commit, git_dirty, recorded_at, sessions,
// reason) and `mdr` added, in a fixed order.
[[nodiscard]] Value makeBaseline(Value set, const BaselineProvenance& provenance, Value mdr);

struct WrittenFiles {
  std::string profileFile;
  std::string baselineFile;
  bool profileCreated = false;
};

// Writes `<recordTo>/<profile.id>/profile.json` (created when absent; an existing one must have the
// same identity, else the write is refused) and the baseline file at the store layout's path. It
// writes where the caller says and nowhere else: the committed store is only ever changed by a
// reviewed PR that commits what this wrote (ADR-0118 D3).
[[nodiscard]] atlantis::Result<WrittenFiles, std::string> writeCandidate(const std::string& recordTo,
                                                                         const Profile& profile,
                                                                         const Value& baseline,
                                                                         std::string_view scene,
                                                                         std::string_view variant,
                                                                         unsigned int width, unsigned int height,
                                                                         std::string_view label);

// The store's guard (ADR-0118 D3): every problem found under `baselinesDir`, as one line each, or
// none. A baseline must be an `atlantis.benchmark.set/1` document recorded under the full protocol,
// with a `baseline` block naming its profile, a commit, its sessions and a reason (category 1 to 4
// and text); it must sit under the profile whose identity its `meta` carries, at the layout's path;
// and nothing else may live in a profile directory. An empty or absent store has no problems.
//
// With `bar`, a baseline whose recorded minimum detectable regression exceeds the record-time quality bar
// (Spec 0060 Correction 2026-10-14) is a problem too: a recording the bar would have refused is not committed.
[[nodiscard]] std::vector<std::string> validateStore(const std::string& baselinesDir, const RecordPolicy* bar = nullptr);

}  // namespace atlantis::runtime::gate
