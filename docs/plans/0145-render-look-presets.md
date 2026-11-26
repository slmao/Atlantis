# Plan 0145 — Render Look Presets and A/B Comparison V1

- **Spec:** [Spec 0145](../specs/0145-render-look-presets.md)
- **ADR:** [ADR-0191](../adr/0191-render-look-preview-and-adoption.md) (Accepted)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** The maintainer approved Spec/Plan 0145, J1–J4 and ADR-0191 on `a528dc09` on 2026-11-26 Singapore time, authorizing S2–S4 after merge. [Approval record](https://github.com/slmao/Atlantis/pull/454#issuecomment-7195842449). The maintainer merged PR #454 as `8bf5fba6`.

## Sequence and expected file scope

| Stage | Changes |
| --- | --- |
| **S1 — Joint review** | Decide J1–J4 and record exact approval. Begin implementation from current `main` after maintainer merge. |
| **S2 — Shared contracts** | Connection authoring/control headers, AuthoringSession guard and candidate validation reuse, Remote codecs/dispatch, CLI, native and C# SDK, MCP and remote protocol reference. Add closed-shape, serialization, compatibility and transaction tests with the existing conformance projects. |
| **S3 — Capture and workflow** | Runtime application/control/authoring host, frame capture and extraction, and focused capture tests. Reuse RenderGraph and renderer entry points with isolated parameters and resources. Editor Worker, model/history and one render-look panel plus local store and focused Editor tests. |
| **S4 — Verification and PR** | Run applicable suites and gates on the final candidate. Use a disposable Bistro scene, native Windows screenshots, original PNGs and raw reports. Publish a reviewable implementation PR. |

The runtime preview work uses the S2 shared types. Keep the renderer shader
algorithms, scene source schema, launcher and global gate configuration out of
the delivery. Finish both client-facing paths before running the Windows matrix.

| Files and modules | Scope |
| --- | --- |
| `src/connection/include/atlantis/connection/{authoring_connection,runtime_control}.h`; `src/authoring/{include/atlantis/authoring,src}/authoring_session.*` | Typed pair request/result, closed guard and whole-candidate validation. |
| `src/remote/`, `src/cli/`, `src/gameplay_sdk/`, `src/csharp/`, `src/mcp/` and `docs/architecture/remote_protocol.md` | Capability advertisement, transport and client exposure. Extend existing client contract and compatibility tests. |
| `src/runtime/src/{frame_capture,runtime_application}.*`, `src/runtime/include/atlantis/runtime/runtime_application.h` and executable-private control/authoring adapters under `src/runtime/` | Freeze validated input, render the pair and release its resources. Extend existing Runtime capture tests. |
| `src/editor_app/Atlantis.Editor.Model/AuthoringDocument.cs`, a new render-look model and store, `src/editor_app/Atlantis.Editor/{EditorSession,MainWindow}.cs` and a new render-look panel partial | Local workflow, late-response protection, transaction settlement and history. Extend existing Editor tests, including fixed-view tests. |

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Stored scope** | Approve one per-user, per-canonical-source and scene-GUID store. Capture all direct lights and the eight active camera fields. Approve full-roster refusal, the directional leaf restriction and fresh rebinding after reopen. |
| **J2 — Preview ownership** | Approve a fresh 0131 PNG pair per explicit Compare, one admitted pair, isolated resources and no live state edits. Confirm that switching previews means switching two completed images. |
| **J3 — Adoption and recovery** | Approve the closed guarded mixed-leaf write, one verified history entry, strict stale confirmation/replay refusal, explicit Apply recovery and existing shared client compatibility. Accept ADR-0191 with J1–J3. |
| **J4 — Acceptance and authorization** | Approve the Windows Night / Daylight matrix and applicable Vulkan, image and performance gates below. After design approval and maintainer merge, authorize S2–S4, scoped fixes, verification and a reviewable implementation PR in one decision. |

## Verification

- [ ] **R1–R2 — Snapshot and local persistence.** Exact typed round trip,
  defaults and zero intensity, name collision, 16 looks and size cap,
  failed or competing replacement, unreadable version, reopen, Save As,
  checkout/source isolation and no document/history/recovery write. Exercise
  missing or replaced GUID, changed parent/placement/kind, extra or absent
  light, wrong active camera, and field drift that is within the look. Refuse
  before capture or adoption when identity or basis cannot be proved.
- [ ] **R3/R5 — Native capture and compatibility.** Check both capabilities
  with old and new clients. Cover correlated pair results, bounded typed
  input and PNG reads, ordinary capture compatibility, Busy, cancellation,
  stale projection, late response and one-side failure. Include a look with
  Bloom enabled when live Bloom resources are absent and compare the ordinary
  capture and current viewport before and after. Use a second ordinary client
  and verify it samples the live scene throughout. Verify scratch resource
  release and report peak memory/service interval on the reference run.
- [ ] **R4 — Atomic write and history.** Check exact guarded admission and
  receipt shape, maximum roster, duplicate/unlisted leaf or addressed target,
  conflict in the last write, camera switch and edit after confirmation,
  and failed/unknown settlement. Verify zero partial writes, no Apply or
  history on refusal, one history item on verified adoption, one Undo/Redo
  targeting the original roster and recovery through explicit Apply without
  reissuing writes. Keep ordinary and legacy batch tests green.
- [ ] **Windows 100% — Prepare one scene and two looks.** Use a disposable
  registered copy of the editable Bistro night rig and the known 0143/0144
  daylight values. Keep one scene GUID, one node/Camera roster, one 0090
  Saved Scenes source and the same studio environment in both looks. Capture
  Night with the original lights and camera values. Use the existing Editor
  controls to make the Daylight values (including the 0144 combination of
  `+1 EV`, Fog density `0.035` and Bloom strength `0.65`) and save its preset.
  Retain the scene/environment identity and typed values. A manually opened
  built-in night scene uses different environment settings and cannot supply
  the Night side of this pair.
- [ ] **Windows 100% — Compare and Cancel.** At both Street Corner and
  Storefront Terrace, capture both looks, switch `A → B → A → B`, then request
  a second fresh pair. Keep the same view and projection matrices within
  `0.0001` per element and 0131 landmark projections within one pixel. Verify
  960 × 540 extent, effective EV/lights and all eight Camera values tied to
  the capture basis. Compare repeated captures of each look within one RGB
  code value per channel. Verify a visible difference between A and B. Cancel
  a held capture and a ready comparison. Check document token, saved bytes,
  history, recovery status and Apply receipt unchanged, including the second
  client's regular captures. Retain native screenshots and raw output pairs.
- [ ] **Windows 100% — Adopt, Save and cold Open.** Establish a settled Night
  projection using the captured values and record the existing history depth.
  Adopt Daylight with one confirmed transaction and compare the applied PNGs
  with the Daylight preview at both views. One Undo restores Night and one
  Redo restores Daylight. Perform explicit Save and independent 0090
  Build/Open from that exact source. Re-read all addressed fields, verify
  scene and GUID identity and recover both Daylight images to the same pixel
  tolerance. Confirm that the local presets can be reselected and that
  Save Configuration never wrote the scene source. Keep the original 0143
  and 0144 fixtures intact.
- [ ] **Applicable gates.** Run affected native Runtime/authoring, shared
  client and Editor suites. Keep Vulkan validation clean with both previews
  and ordinary capture. Run the applicable headless/image regression checks
  and retain any required golden review. Use `--gate --applies` on the final
  diff and execute any selected performance gate under the existing protocol.
  Preserve raw determinations and input provenance.

## Rollback and definition of done

An ordinary revert withdraws the new capabilities and UI. Adopted values
remain ordinary scene fields editable through existing workflows. The local
store is independent of scene files and Build/Open. Apply the repository
[definition of done](../process/definition-of-done.md).
