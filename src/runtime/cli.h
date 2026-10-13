#pragma once

// Private to atlantis_runtime's own startup layer. NOT under
// src/runtime/include/atlantis/runtime/ (Atlantis::RuntimeHost's own
// public directory), NOT compiled into atlantis_runtime_host --
// ADR-0076 Decision 2. The same cli.cpp this header declares is
// compiled directly into both atlantis_runtime and the existing
// GPU-independent atlantis_runtime_tests target (Spec 0032
// Requirement 1) -- a shared source file across two executable
// targets, not a library, not a new CMake target.

#include <atlantis/asset_system/asset_guid.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace atlantis::runtime::cli {

// The BootstrapConfig input this Spec's own CLI surface varies (Spec
// 0032 Requirement 5): which catalog scene to load (Plan 0047 P14 -- the
// scene's GUID, resolved through the one build catalog). Every other
// BootstrapConfig field (all shader-pair paths, environment paths, the
// catalog path, enableValidationLayers, applicationName) is untouched by
// this type or by parseCommandLine() below -- a structural guarantee, not
// something a test needs to separately assert.
struct SceneSelection {
  atlantis::asset_system::AssetGuid sceneAsset;
  // Plan 0046 Milestone 3 (ruling O1, Plan 0046 P10): the entry renders
  // without the global environment (no IBL, no sky) -- lit by its own
  // lights only. Travels with the selected scene so main() can act on it.
  bool disableEnvironmentLight = false;
};

// One whitelist entry. `name` is a non-owning view: every real caller
// (main.cpp) constructs entries from string literals (the three fixed
// scene names have static storage duration), so `name` trivially
// outlives any parseCommandLine() call; `selection` is owned by the entry.
// Borrowed access never implies ownership transfer (AGENTS.md
// Ownership rules) -- the `whitelist` span parseCommandLine() takes
// below is valid only for the duration of that one call; every string
// parseCommandLine() returns is a fresh owned copy, never a view into
// argv or whitelist.
struct SceneWhitelistEntry {
  std::string_view name;
  SceneSelection selection;
};

// The name selected when --scene is omitted -- matches today's
// unconditional atlantis_runtime default, byte-for-byte (Spec 0032
// Requirement 3). The caller's whitelist MUST contain an entry with
// this exact name; parseCommandLine() treats a missing default as a
// caller/programmer-error precondition violation (assert), never a
// runtime error -- that distinction matches AGENTS.md's Error
// handling rule ("programmer errors are assertions, not error
// returns"; malformed --scene *user* input, by contrast, is real
// untrusted input and always produces a PrintErrorAndExit result,
// never an assertion).
inline constexpr std::string_view kDefaultSceneName = "integrated_showcase_demo";

// Spec 0059 / ADR-0117 D3: the benchmark's diagnostic variants -- a closed list.
enum class BenchmarkVariant {
  Baseline,       // the scene as authored; the only committed baseline
  NoPointLights,  // every point light's Light component removed
  NoFog,          // the camera's fog density set to 0
  NoBloom,        // the camera's bloom strength set to 0
};
[[nodiscard]] std::string_view toString(BenchmarkVariant variant) noexcept;

// Spec 0059 / ADR-0117 D1: `--benchmark <scene> [...]`. A scene run that measures
// instead of serving a client: the scene is rendered in the real windowed loop at
// a requested client size, for `warmup` then `frames` presented frames, and a JSON
// document is produced. Mutually exclusive with --scene, --exec, --listen,
// --session-file and --editor.
struct BenchmarkRequest {
  std::string sceneName;
  // Empty if `sceneName` is not on the whitelist (the Bistro entry exists only
  // when its content was fetched at configure time); the caller reports that as
  // an environment mismatch, not a usage error.
  std::optional<SceneSelection> scene;
  unsigned int warmup = 120;
  unsigned int frames = 300;
  unsigned int width = 1920;
  unsigned int height = 1080;
  BenchmarkVariant variant = BenchmarkVariant::Baseline;
  bool json = false;                       // the document on stdout (else a short summary)
  std::optional<std::string> outputPath;   // the document to a file as well
  std::optional<std::string> baselinePath; // compare against this earlier document
  bool series = false;                     // include the raw per-frame values
  bool gpuTiming = true;                   // --gpu-timing=on|off
  // Plan 0060 M4 (ADR-0119 D1): `--warmup-seconds <s>` -- the warmup phase continues until
  // `warmup` frames AND this many seconds have elapsed (default 0: warmup frames only). It
  // exists so the gate's preheat is a duration, not a frame count; nothing measured changes.
  double warmupSeconds = 0.0;
  // Owner ruling on the Plan 0060 M7 stop (2026-10-11): `--pin-process on|off` -- inside this process
  // only, opt out of power throttling and raise the priority class (process-level measurement-environment
  // control; nothing in the measured loop changes). Default off, so a plain --benchmark behaves as before.
  bool pinProcess = false;
};

// Plan 0060 M3 (ADR-0119 D5): `--gate [...]`, `--gate --record ...` and `--gate --applies`.
// Mutually exclusive with --benchmark, --exec, --listen, --session-file, --editor, --help
// and --list-scenes. The gate itself runs no frames: it launches child --benchmark runs.
struct GateRequest {
  enum class Mode {
    Gate,     // run the protocol and judge it against the stored baseline
    Record,   // run the protocol over several sessions and write a candidate baseline
    Applies,  // read changed paths on stdin and say whether the applicability file selects them
  };
  Mode mode = Mode::Gate;
  std::string sceneName = "bistro";
  // Empty if `sceneName` is not on the whitelist (the caller reports an environment mismatch,
  // as for --benchmark).
  std::optional<SceneSelection> scene;
  BenchmarkVariant variant = BenchmarkVariant::Baseline;
  unsigned int width = 1920;
  unsigned int height = 1080;
  std::optional<unsigned int> runs;            // --runs: the policy's value when absent
  std::optional<double> preheatSeconds;        // --preheat-seconds: the policy's value when absent
  bool idleWait = true;                        // --idle-wait on|off
  std::optional<bool> pinProcess;              // --pin-process on|off: the policy's value when absent
  std::optional<bool> requireMains;            // --require-mains on|off: the policy's value when absent
  std::optional<std::string> baselinesDir;     // --baselines
  std::optional<std::string> profileId;        // --profile <id>|auto (auto: match this machine)
  std::optional<std::string> policyPath;       // --policy
  bool json = false;                           // the verdict on stdout (else a short summary)
  std::optional<std::string> outputPath;       // the verdict (or candidate baseline's report) to a file as well
  // --record only.
  unsigned int sessions = 3;                   // --sessions
  double sessionGapMinutes = 10.0;             // --session-gap-minutes
  unsigned int reasonCategory = 0;             // --reason-category 1..4 (ADR-0118 D3)
  std::string reasonText;                      // --reason
  std::string recordTo;                        // --record-to: the directory the candidate baseline goes to
  // --applies only.
  std::optional<std::string> applicabilityPath;  // --applicability
};

enum class CommandLineOutcome {
  RunScene,           // .selectedScene is populated; proceed to BootstrapConfig/createRuntimeApplication().
  PrintUsageAndExit,  // .message is --help or --list-scenes text; print to stdout, exit RuntimeExitReason::Success.
  PrintErrorAndExit,  // .message is a diagnostic + usage; print to stderr, exit RuntimeExitReason::InitializationFailed.
};

struct CommandLineResult {
  CommandLineOutcome outcome;
  std::string message;                              // empty when outcome == RunScene
  std::optional<SceneSelection> selectedScene;  // populated only when outcome == RunScene
  // Spec 0054 ruling Q3 (Plan 0054 P7): `--exec <path|->` -- a CLI script to
  // run against the scene (a file path, or "-" for stdin). Populated only
  // when outcome == RunScene and --exec was given.
  std::optional<std::string> execScript;
  // Spec 0055 R1 (Plan 0055 P3, P4): `--listen <port>` -- serve attachable
  // clients on 127.0.0.1:<port> (0: an ephemeral port); `--session-file
  // <path>` -- where the session file goes (only with --listen; default
  // ./.atlantis/runtime.session.json). Populated only when outcome ==
  // RunScene and the flag was given. Without --listen no socket is opened.
  std::optional<std::uint16_t> listenPort;
  std::optional<std::string> sessionFile;
  // Spec 0056 ruling Q2 (Plan 0056 P10, J9): `--editor` -- host Atlantis
  // Editor on the scene. Combines with --scene and --listen; refused with
  // --exec, --help and --list-scenes. Meaningful only when outcome ==
  // RunScene.
  bool editor = false;
  // Spec 0059 / ADR-0117 D1: populated only when outcome == RunScene and
  // --benchmark was given; then selectedScene is empty and execScript, listenPort,
  // sessionFile and editor are unset.
  std::optional<BenchmarkRequest> benchmark;
  // Plan 0060 M3: populated only when outcome == RunScene and --gate was given; then
  // selectedScene and benchmark are empty.
  std::optional<GateRequest> gate;
};

// Pure function: no I/O, no std::exit, no read of any CMake macro,
// environment variable, or other process state beyond argc/argv and
// whitelist (Spec 0032 Requirement 5) -- this is what lets a
// GPU-independent test call it with a fixture-only whitelist instead
// of real build paths. `whitelist` must contain exactly the three
// entries Spec 0032 Requirement 2 fixes (order does not affect lookup
// correctness, but fixes --list-scenes' own output order -- callers
// SHOULD use Requirement 2's stated order: integrated_showcase_demo,
// ibl_material_demo, pbr_normal_map_demo) and must include an entry
// named kDefaultSceneName. parseCommandLine() does not itself enforce
// whitelist size or name uniqueness beyond that one precondition --
// exactly the same caller-responsibility contract BootstrapConfig's
// own header comment already establishes for its caller-populated
// fields.
[[nodiscard]] CommandLineResult parseCommandLine(int argc, char** argv,
                                                  std::span<const SceneWhitelistEntry> whitelist);

}  // namespace atlantis::runtime::cli
