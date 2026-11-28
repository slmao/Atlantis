# Plan 0150 — Scene Environment Lighting and Reflections V1

- **Spec:** [Spec 0150](../specs/0150-scene-environment.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** [Maintainer joint approval](https://github.com/slmao/Atlantis/pull/463#issuecomment-7236980882) of Spec/Plan 0150 and J1–J4 on 2026-11-28, authorizing S2–S4 after [maintainer merge](https://github.com/slmao/Atlantis/pull/463).
- **Related ADR:** [ADR-0196](../adr/0196-scene-environment-selection-and-activation.md) (Accepted)

## Objective

Select a complete, existing environment for a hand-authored scene, compare it at
a fixed bookmark, and confirm that the saved scene reproduces it when opened
independently.

## Milestones and sequencing

1. **S1 — Joint review.** Resolve J1–J4 and accept ADR-0196. Begin S2 on
   current `main` once the maintainer has merged the reviewed design.
2. **S2 — Authoring and startup.** Implement the source/cooked versions and
   environment dependency edge. Expose the guarded operation and readbacks
   consistently to the Editor, CLI, native and managed SDKs, and MCP. Verify
   both environment artifacts are built with the normal catalog and available
   to Saved Scenes. Resolve and validate the effective choice for cold startup
   before selecting its pipeline family.
3. **S3 — Apply and Editor.** Prepare and activate a compatible IBL candidate
   during the existing reload with the receipt semantics in ADR-0196. Add the
   single scene control, errors and history. Use normal bookmark captures and
   the existing independent launch, with environment identity tied to the
   settled generation.
4. **S4 — Verification and implementation PR.** Verify the final revision
   under the checklist. Cite the implementation diff and retained evidence
   and hand off to the maintainer for manual merge.

## Expected files and module scope

| Files or modules | Work |
|---|---|
| `src/asset_system/`, `src/tools/asset_cooker/`, `assets/CMakeLists.txt`, `cmake/AtlantisManagedImports.cmake` | Scene source, decoded/cooked and authoring mapping, asset kind/identity validation, catalog dependency, producer coverage and saved-scene build inputs. |
| `src/connection/`, `src/authoring/`, `src/remote/`, `src/cli/`, `src/gameplay_sdk/`, `src/csharp/`, `src/mcp/`, `docs/architecture/remote_protocol.md` | Guarded scene edit, additive readbacks and capability, exact wire/SDK/CLI/MCP compatibility and errors. |
| `src/runtime/CMakeLists.txt`, `src/runtime/main.cpp`, `src/runtime/authoring_host.*`, `src/runtime/src/{bootstrap_config,scene_load,runtime_application,runtime_control_host,environment_realization,frame_capture}.cpp` and corresponding Runtime headers | Cold/active catalog selection, apply settlement and isolated candidate upload, recorded environment identity and matching normal capture. Keep Windows saved-slot launch and supported packaging inputs coherent. |
| `src/editor_app/Atlantis.Editor/`, `src/editor_app/Atlantis.Editor.Model/` | Scene Environment control, authoring history, independent build/open and status readbacks. Reuse 0090 and 0147 routes and existing Avalonia resources. |
| Existing asset/cooker/catalog, Authoring, Runtime, Remote, CLI, SDK, MCP, Editor, image and packaging suites | Targeted tests and a disposable controlled scene; unchanged golden and performance references. |

## Joint review decisions

| Item | Recommendation |
|---|---|
| **J1 — Scope and precedence** | Use one packaged environment choice. Inherit follows startup policy, an explicit valid GUID takes priority, and explicit resolution failure is visible. Use Studio and Warehouse for the first acceptance. |
| **J2 — Persistence and clients** | Approve ADR-0196's version boundaries, dependency validation, guarded scene edit and additive shared readbacks. Preserve older sources and unrelated edits. |
| **J3 — Activation and recovery** | Support IBL-to-IBL replacement under the settled Apply boundary. Save and independently Build/Open across the IBL family boundary. Preserve the valid projection and history on recoverable failures. |
| **J4 — Acceptance and authorization** | Approve the Windows 100% comparison, independent launch and gate scope below. One explicit sign-off can authorize implementation, in-scope repairs, applicable verification and a review-ready PR after the maintainer merges this design. |

## Verification checklist

- [ ] **R1/R2 — Format, compatibility and dependency.** Round-trip old source
  versions 7–10 and cooked v7/semantic2, an explicit v11/v8 choice with and
  without Prefab fields, and a return to Inherit. Verify Save As and recovery
  preserve the chosen GUID, and Prefab operations leave it scene-local.
  Cover old cooker/Runtime refusal of explicit versions, an older shared
  client reading ordinary status and Apply failures and performing an unrelated
  edit and save,
  and new clients treating absent capability as unsupported. Check cold
  startup without an override on Studio-default and no-environment
  launch profiles, and explicit precedence with default present and absent.
  Remove the dependency or corrupt its type, schema, metadata or artifact in
  disposable input. The cold Build/Open or startup must report the failed
  environment and must not activate a substitute. Verify the edited saved
  slot and its environment assets/outputs participate in the current build
  receipt check.
- [ ] **R1/R3 — Transaction and lifetime.** Exercise exact read/commit in CLI,
  both SDKs and MCP, double confirmation, unchanged choice, stale revision or
  catalog, missing GUID and disconnect after send. Require zero writes for a
  rejected edit and no automatic retry when the outcome is unknown. On an
  in-scope live replacement, confirm old-generation frames and captures
  remain coherent while upload is pending and reject stale UI callbacks after
  settlement. Cover recoverable loader, allocation and upload preparation
  failure, pending completion, and fatal loss according to existing Runtime
  handling. Verify no candidate resources are released before completion.
  An unsuccessful Apply retains the old effective choice and version.
  A family crossing shows the explicit Build/Open instruction and does not
  report success. Exercise one Undo/Redo and explicit Apply after a corrected
  failure.
- [ ] **R4 — Windows 100% controlled A/B.** Use a disposable saved hand-authored
  scene with a clear sky patch, a rough neutral dielectric receiver and a smooth
  metallic receiver both using existing opaque PBR materials. Pick one 0147
  bookmark at `960 × 540` physical pixels. Record three fixed interior pixel
  masks and the comparison threshold before switching
  (at least 32 pixels per mask and median absolute RGB change of at least
  5 levels in one channel per mask). Fix direct-light values, material
  bindings and factors, camera GUID, exposure, fog and Bloom. Commit and Apply
  explicit Studio → Warehouse → Studio; capture once after each settled
  projection and record environment GUID, document revision, Apply receipt,
  generation, dimensions and view/projection matrices. Verify matrices within
  `0.0001` elementwise, unchanged control inputs, and visible measurable
  differences in the sky, dielectric and smooth-metal masks for A versus B.
  Compare both Studio captures within one RGB level per mask pixel. Retain
  original PNGs, native panel screenshot and region metrics. These are
  observations of differently sensitive surfaces under fixed inputs.
- [ ] **R3/R4 — Windows 100% persistence.** Select Warehouse again and save
  via Editor. Use the normal Saved Scenes independent Build/Open, open the
  same bookmark and check saved and effective GUID, fixed inputs, matrices
  and physical size. On the same reference machine compare the settled
  Warehouse capture with the prior Warehouse image within one RGB level in
  the same masks and report the full-frame comparison. Verify the old Editor
  window and its scene remain independently usable. Reopen an unchanged
  legacy scene to verify its default remains compatible.
- [ ] **Final gates.** Run affected native and managed build and test suites,
  clean Vulkan validation for startup, replacement and failure, applicable
  headless and existing IBL/sky golden checks, Android build/package on the
  shared input boundary, and the existing performance applicability check.
  Run the selected original gate protocol and retain its raw verdict.
  Reuse evidence only when the inputs still match.

## Rollback and definition of done

Revert the authoring, startup and Apply support together. Before reopening an
explicit v11 scene with an older toolchain, clear the environment override with a
supporting build and save, or restore a pre-change source. Rebuild using that
version's normal Saved Scenes flow. See the repository
[definition of done](../process/definition-of-done.md).
