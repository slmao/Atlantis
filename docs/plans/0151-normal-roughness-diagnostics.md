# Plan 0151 — Normal and Roughness Diagnostics V1

- **Spec:** [Spec 0151](../specs/0151-normal-roughness-diagnostics.md)
- **Related ADR:** [ADR-0197](../adr/0197-isolated-normal-roughness-diagnostic-capture.md) (Proposed)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending

## Objective

Expose the actual fragment shading normal and effective roughness of one
located opaque PBR slot next to ordinary shading from the same bookmark
and settled projection.

## Milestones and dependencies

1. **S1 — Joint review.** Resolve J1–J4 and ADR-0197. Begin implementation
   from current `main` after joint approval and maintainer merge.
2. **S2 — Shared contract and isolated capture.** Add the bounded three-image
   request, strict client codecs and capability. Resolve the shared target
   and use one capture interval. Wire the existing normal scene capture,
   the two diagnostic draws and verified output publication. Establish
   analytic headless cases and preserve 0148.
3. **S3 — Editor.** Add the action beside Compare Base Color at the resolved
   material slot. Reuse 0138/0147/0148 guards, comparison layout and image
   retirement. Show output labels, legend and binding provenance.
4. **S4 — Verification and PR.** Collect the Windows and shared-client
   evidence below. Run affected tests and applicable gates. Submit a
   reviewable implementation PR with the candidate and verification links.

S2 assumes the merged 0138, 0146–0148 capture and binding services.
S3 consumes the new shared capability and result. Test fixtures and
client conformance cases can be prepared alongside S2.

## Expected files and module scope

| Files and modules | Work |
| --- | --- |
| `src/connection/include/atlantis/connection/runtime_control.h`, `src/remote/`, `src/cli/`, `src/csharp/`, `src/mcp/`, `docs/architecture/remote_protocol.md` | Shared additive operation, native client and managed SDK, CLI, exact MCP tool and protocol reference. Reuse the native RuntimeControl surface used by C++ callers. |
| `src/runtime/main.cpp`, `src/runtime/src/{frame_capture,runtime_control_host}.cpp`, matching Runtime headers and existing capture-local resource definitions | Resolve the settled binding and texture inputs. Reuse capture admission, reports, private scratch, readback, file publication and completion. |
| `src/renderer/{include,src}/`, new capture-only Slang shaders and existing PBR DirectLit/IBL variants, root, shader and Runtime CMake wiring | Use production input math and raster behavior, RenderGraph passes, and Windows-only diagnostic shader staging. Touch existing shader bodies only for equivalent shared input expressions if needed. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorSession,MaterialDiagnosticWindow,ViewBookmarkWindow}.cs`, existing Editor Model and tests | Reuse target, bookmark and stale-result checks. Display three views and their provenance. |
| Existing Remote, CLI, SDK, MCP, Runtime, Renderer, shader and image regression tests including their CMake registration, plus disposable scene/material fixtures | Cover compatibility, bounded lifetimes, analytic pixel expectations and Windows workflow. |

## Joint review decisions

| Item | Recommendation |
| --- | --- |
| **J1 — Coverage and entry** | One uniquely located opaque `PbrDirectLit` binding at a settled bookmark. Show ordinary full-scene color and two isolated views of that binding. Preserve the existing base-color action. |
| **J2 — Numeric display** | World XYZ mapped into RGB UNORM for the shading normal. Grayscale UNORM for linear packed G times the roughness factor, with factor-only behavior when unbound and factor default 1.0. |
| **J3 — Contract and resources** | Accept ADR-0197's compatible, shared three-image request, capture-owned RenderGraph work and refusal/retirement semantics. |
| **J4 — Acceptance and authorization** | Approve the Windows 100% analytic and Bistro runs and applicable gates. One sign-off can authorize S2–S4, ordinary in-scope fixes, verification and a reviewable PR after maintainer merge of the design. |

## Verification checklist

- [ ] **R1/R3 — Target and compatibility.** Exercise the old pair on a new
  Runtime and the new controls against an older one. Cover capability, exact
  request and reply lengths, roles and path/extent/matrix correlation in CLI,
  C++ and C# SDKs and MCP. Test ambiguous and unsupported targets, changed
  slot, document, Apply, viewport policy and bookmark, and a delayed reply
  after the Worker has observed the next projection. Preserve selection,
  draft, camera, history, source, revision and Apply state. Refuse missing
  required cooked data. After loss of a sent receipt, report unknown and
  check-before-retry guidance.
- [ ] **R2 — Analytic headless and Windows 100% controlled scene.** Use a
  disposable opaque fixture at a 960 × 540 physical bookmark, and retain
  one fixed interior region with at least 32 pixels. Record the cooked input,
  factor and expected byte triples before capture. Use constant texels or
  known interior samples. First vary only the normal input, including a
  positive tangent-space Y component. Check the expected world-space
  quantization within two levels per RGB channel and require the roughness
  image to be pixel-identical. In a separate trial vary only packed G,
  with R/B/A and the factor held fixed. Check the expected grayscale within
  one byte and require the normal image to be pixel-identical. With a
  packed G of 128/255 and factor 0.5, the expected byte is 64. Cover a
  factor-only case (0.25 maps to about 64), omitted factor (1.0 maps to
  255), and an unbound normal map. Keep both diagnostic masks identical.
  Confirm ordinary captures match a current normal capture at that
  observation and show an observable change under deliberately responsive
  light and view geometry. Include transformed tangents and negative cooked
  handedness, nonuniform transform, non-16:9 extent, existing mip sampling
  and DirectLit/IBL paths in focused tests. Vary only lighting/appearance
  and confirm both data PNGs are unchanged.
- [ ] **R2 — Windows 100% Bistro.** Use a disposable editable Bistro copy.
  At a saved 960 × 540 bookmark locate one stone-paving or tabletop
  surface through 0138. Check that it is a supported slot before selecting
  the sample. Record the node, slot, mesh/material GUIDs, optional map GUIDs,
  factor, UV/sample evidence for a covered patch, and settled revision and
  generation. Retain a native comparison screenshot and all three raw
  PNGs. Report the material variation observed or the actual factor-only
  case based on the resolved binding. Use the controlled scene to prove
  independent map effects. Restore the bookmark after navigation and
  repeat to verify exact extent, matrices within 0.0001 per element and
  stable diagnostic pixels at the same settled inputs.
- [ ] **R3 — Isolation and gates.** On a stable projection compare a second
  client's ordinary captures before and after the triplet, with no scene,
  material or history writes. Check budget refusal at extreme/invalid
  inputs, maximum valid extent, private allocation and completion, a
  failed diagnostic recording or wait, and a conflict opening the third
  output. Prove no complete result and no overwrite, and verify safe
  scratch release under ADR-0192. Run affected native and managed
  build/tests, Vulkan validation, headless/image and existing PBR/Bistro
  goldens, and Android build/packaging if shared shader or native inputs
  are affected. Check performance applicability and run the selected
  original gate protocol if applicable. Retain its verdict and unchanged
  baseline and thresholds.

## Rollback and definition of done

Revert the new capability, capture-only shaders and comparison action
together. Existing settled scene data, bookmarks and base-color callers
retain their formats. Apply the [definition of done](../process/definition-of-done.md).
