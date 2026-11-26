# Plan 0146: Isolated Descriptors for Render Look Capture

- **Spec:** [Spec 0146](../specs/0146-render-look-capture-descriptors.md)
- **ADR:** [ADR-0192](../adr/0192-render-look-capture-descriptor-ownership.md) (Accepted)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** The maintainer approved Spec/Plan 0146, J1–J4 and ADR-0192 at `56207a64` and authorized S2–S4 on PR #455 after merge. [Approval record](https://github.com/slmao/Atlantis/pull/456#issuecomment-7196813749). The maintainer merged PR #456 as `f2859a1f`.

## Objective and sequence

Resolve the descriptor blocker in [PR #455](https://github.com/slmao/Atlantis/pull/455)
and complete the remaining [Plan 0145](0145-render-look-presets.md) verification
on the same implementation PR.

1. **S1 — Review.** Approve J1–J4 and record the exact design revision.
   After maintainer merge, integrate latest `main` into the existing
   implementation branch.
2. **S2 — RHI and Vulkan.** Add the opt-in recording path, bounded private
   pool, set-free capture pipeline creation and retirement at completion.
   Verify allocation refusal, descriptor binding and submission lifetime.
3. **S3 — Runtime capture.** Enumerate the pipelines and attachment inputs
   from the frozen scene and both looks. Connect one-at-a-time capture to
   the existing RenderGraph path. Verify ordinary capture and Bloom state
   around failures and cancellation. Complete the remaining 0145 client
   and Editor workflow under its existing file scope.
4. **S4 — Verification.** Verify both Bistro views, the independent-client
   workflow and all applicable gates on the final candidate. Attach the
   evidence to #455. Readiness depends on both plans' required checks.

## Expected file scope

| Files | Scope |
| --- | --- |
| `src/rhi/include/atlantis/rhi/{device,command_list,types}.h` | Explicit isolated factory, capture-only pipeline option and fallible recording outcome. |
| `src/vulkan_backend/src/vulkan_{device,command_list,pipeline,result}.{h,cpp}` | Use existing pipeline layouts, allocate the private pool and sets, preserve normal allocation, and drain retained capture recordings. |
| `src/runtime/src/{frame_capture,runtime_application}.*` and `src/runtime/include/atlantis/runtime/runtime_application.h` | Build the bounded pipeline list, keep capture scratch and borrowed resources alive, and settle or refuse the pair. |
| `tests/vulkan_backend/` and `tests/runtime/` capture and submission fixtures | Extend existing fixtures for isolation, bounded allocation, Bloom and release paths. |

## Joint review decisions

| Item | Recommendation |
| --- | --- |
| **J1 — Boundary** | Approve ADR-0192's explicit RHI recording mode, Vulkan-owned private descriptors and opt-in capture-only creation for missing Bloom pipelines. |
| **J2 — Budget** | Approve 320 sets, 320 UBO descriptors and 2,560 combined samplers, with one active pool and no fallback. Approve the documented refusal paths. |
| **J3 — Settlement** | Approve serialized A then B, completion-owned release, and retention or terminal teardown on uncertain completion. Cancel only retires the caller's result. |
| **J4 — Verification and authorization** | Retain the Windows 100% 0145 matrix and final gates. After approval and maintainer merge, authorize S2–S4, scoped fixes, verification and completion of the original implementation PR in one decision. |

## Verification checklist

- [ ] **R1–R2 — Allocation and bindings.** Check duplicate pipeline
  declarations, interleaved repeated draws, exact budget and over-limit
  refusal. Exercise pool and set allocation failure and reject an
  undeclared pipeline before submission. Verify that isolated bindings
  use the declared layout and that ordinary pool occupancy, allocation
  schedule and frame profiling are unchanged by capture. Verify the
  missing-Bloom path acquires no ordinary pool set.
- [ ] **R3 — Lifetime.** Cover unsubmitted discard, accepted submit with
  deliberately delayed completion, cancellation and wait failure. Check
  release of the command list and pool before scratch and device teardown.
  Verify that a failed wait suppresses publication and keeps ownership,
  and that a second variant or pair cannot reuse those resources.
- [ ] **R1–R4 — Windows 100% Bistro.** Use the full saved Bistro scene and
  both 0131 fixed views. Run Night/Daylight with live Bloom off and one
  variant on, then verify both Bloom states without changing live settings.
  Follow Plan 0145 for A/B/A/B, a second fresh pair, 960 × 540 captures,
  stable matrices and landmarks, repeated image tolerance, Cancel,
  and Adopt through independent Save and Build/Open. Keep a second client
  connected for ordinary queries and captures before and after each
  outcome. A probe received during capture may settle after the serialized
  pair. Verify its pixels and scene/report identity are from the live
  scene and that capture did not change the document, history or Apply.
  Record the actual peak set count, scratch usage and capture service
  interval with the original images and native screenshots.
- [ ] **R4 — Final gates.** Run affected RHI/Vulkan, Runtime and 0145
  client and Editor checks. Retain clean Vulkan Validation Layers,
  applicable headless/image regression and golden review, and the
  existing performance applicability and protocol on the final diff.
  Apply the existing Android build/package rules to shared Vulkan/RHI
  changes. Reuse earlier evidence only if its inputs still match.

## Rollback and definition of done

Reverting capture isolation withdraws look-pair capture until an isolated
path is available. Ordinary capture remains on the existing path.
Apply the repository [definition of done](../process/definition-of-done.md)
and Plan 0145's acceptance requirements before #455 is review-ready.
