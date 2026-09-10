#pragma once

// Private to atlantis_runtime's own startup layer. NOT under
// src/runtime/include/atlantis/runtime/ (Atlantis::RuntimeHost's own
// public directory), NOT compiled into atlantis_runtime_host --
// ADR-0076 Decision 2. The same cli.cpp this header declares is
// compiled directly into both atlantis_runtime and the existing
// GPU-independent atlantis_runtime_tests target (Spec 0032
// Requirement 1) -- a shared source file across two executable
// targets, not a library, not a new CMake target.

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace atlantis::runtime::cli {

// Owned copy of exactly the three BootstrapConfig fields this Spec's
// own CLI surface varies (Spec 0032 Requirement 5). Every other
// BootstrapConfig field (all shader-pair paths, environment paths,
// enableValidationLayers, applicationName) is untouched by this type
// or by parseCommandLine() below -- a structural guarantee, not
// something a test needs to separately assert.
struct SceneBootstrapPaths {
  std::string sceneArtifactPath;
  std::string sceneMetadataPath;
  std::string sceneDependencyManifestPath;
};

// One whitelist entry. `name` is a non-owning view: every real caller
// (main.cpp) constructs entries from string literals (the three fixed
// scene names have static storage duration), so `name` trivially
// outlives any parseCommandLine() call; `paths` is owned by the entry.
// Borrowed access never implies ownership transfer (AGENTS.md
// Ownership rules) -- the `whitelist` span parseCommandLine() takes
// below is valid only for the duration of that one call; every string
// parseCommandLine() returns is a fresh owned copy, never a view into
// argv or whitelist.
struct SceneWhitelistEntry {
  std::string_view name;
  SceneBootstrapPaths paths;
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

enum class CommandLineOutcome {
  RunScene,           // .selectedScene is populated; proceed to BootstrapConfig/createRuntimeApplication().
  PrintUsageAndExit,  // .message is --help or --list-scenes text; print to stdout, exit RuntimeExitReason::Success.
  PrintErrorAndExit,  // .message is a diagnostic + usage; print to stderr, exit RuntimeExitReason::InitializationFailed.
};

struct CommandLineResult {
  CommandLineOutcome outcome;
  std::string message;                              // empty when outcome == RunScene
  std::optional<SceneBootstrapPaths> selectedScene;  // populated only when outcome == RunScene
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
