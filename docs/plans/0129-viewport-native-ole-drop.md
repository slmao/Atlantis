# Plan 0129 — Native OLE Target for Viewport Mesh Placement

- **Spec:** [Spec 0129](../specs/0129-viewport-native-ole-drop.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** 2026-11-21, [Spec/Plan 0129, J1–J4 and gated implementation approved](https://github.com/slmao/Atlantis/pull/421#issuecomment-7060287245) at `e08987ea`. Maintainer merge `a36518d4`.
- **ADR:** [ADR-0178](../adr/0178-editor-owned-ole-viewport-drop.md) (Accepted)

## Objective and sequencing

Unblock the native-input prerequisite of [Plan 0128](0128-viewport-mesh-placement.md).
The baseline is `a7d0004b`, the maintainer merge of
[PR #420](https://github.com/slmao/Atlantis/pull/420). Preserve its
[failed route evidence](https://github.com/slmao/Atlantis/pull/420#issuecomment-7060085719).

1. **Joint review.** Approve this supplement and its limited ADR change.
   Start from latest main after the maintainer merges the design.
2. **S2 prerequisite.** Prove ADR-0178's temporary Editor-owned OLE target
   with Plan 0129's Windows gate. Use the real Assets source, embedded
   Runtime and proposed COM adapter. A disposable observation sink may
   report Copy for the matched local token so the OS delivers a real
   Drop. The sink records events and never queues scene edits.
3. **On pass, resume S2–S4.** Connect the verified route to the previously
   approved preview and settlement. Execute the remaining
   [Plan 0128 checks](0128-viewport-mesh-placement.md#verification-checklist),
   including Bistro terrace placement, cancellation and stale rejection,
   Undo/Redo and Save/reopen. Retain native screenshots and applicable
   validation and performance gates. Submit a review-ready implementation
   PR for maintainer merge.
4. **On failure, stop.** Preserve the raw result and existing #420
   evidence. Report the failed criterion and any required design or
   boundary change. Keep Bistro and dependent implementation pending.

## Expected files and boundaries

These are additions to the file scope already approved in Plan 0128.

| Files | Scope |
| --- | --- |
| `src/editor_app/Atlantis.Editor/ViewportOleDropTarget.cs` (new) | Private HWND, OLE registration, token interop and teardown. |
| `src/editor_app/Atlantis.Editor/{MainWindow,ViewportWindow}.cs` | Attach the target to the existing drag and native host lifetime. Route retired and valid gestures through the existing guards. |
| `src/editor_app/Atlantis.Editor/Atlantis.Editor.csproj` | Windows Editor-only compiler allowance for generated COM stubs. No package or built-in COM setting changes. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{ViewportMeshPlacementTests,ViewportMeshPlacementUiTests}.cs` | Extend Plan 0128's proposed tests with native route/lifetime coverage. Keep temporary fixtures and Windows evidence outside the product tree. |
| Spec/Plan 0129 and ADR-0178 approval metadata, `docs/specs/README.md` | Approval and navigation. |

Any proposed change to the Runtime child procedure, input forwarding,
foreign-window registration/ownership, shared interfaces, dependencies or
additional build policy requires review before proceeding.

## Joint review recommendations

| Decision | Recommendation |
| --- | --- |
| **J1 — Window and ownership** | Approve one Editor-owned, clipped and nonactivating layered sibling for an eligible local Assets drag. Accept the transient composition trade-off subject to Windows evidence. Keep the Runtime HWND boundary. |
| **J2 — Admission and cleanup** | Approve the shared one-use token, private Windows byte format and single Editor project allowance for generated COM stubs. Require matched input, ordered teardown, and immediate restoration of ordinary input. |
| **J3 — Pass or stop** | Require the Windows 100% gate below with real pointer input and retained evidence. Stop on a failed criterion or any change beyond ADR-0178. |
| **J4 — One authorization** | After joint approval and maintainer merge, authorize the gate and, only after it passes, the remaining 0128 S2–S4, in-scope fixes, verification and a review-ready PR. Final merge stays with the maintainer. |

Suggested authorization:

> Approve Spec/Plan 0129 and J1–J4 and accept ADR-0178. After maintainer
> merge, prove the OLE route on Windows 100% using the actual embedded
> Runtime child and real mouse input. On pass, continue 0128 S2–S4 through
> Bistro acceptance and a review-ready PR under the current gates. On
> failure, preserve the evidence and stop. Final merge remains manual.

## Windows 100% route gate

Use one bounded gate run on the reference desktop. Record the Editor and
Runtime revisions, measured DPI, current host/Runtime/target HWNDs and
physical points correlated with the callbacks. Keep the native screenshots
and raw pass or failure report. A small hand-authored fixture suffices for
this route gate. Bistro remains mandatory for the implementation PR.

- [ ] **Real hover, leave, re-entry and release (R1/R3).** Use the actual
  Assets row and move the held pointer onto the visible embedded Runtime,
  out to the existing Hierarchy target and back, then release over the
  drawable. Require ordered Enter/Over/Leave/re-entry/Drop, with one Drop
  delivered to the active local token and the physical release coordinates
  inside the clipped drawable. Establish the target is above the real child
  during the drag, the scene remains visible, and the existing Hierarchy or
  Inspector routing remains usable outside the drawable.
- [ ] **Cancel and restoration (R2/R3).** Press Escape during a fresh drag
  over the drawable. Verify zero admitted drops and that the temporary HWND
  no longer intercepts input. Immediately click a known Runtime mesh and
  exercise a normal Gizmo handle with real pointer input. Verify the expected
  selection and handle response, cancel the Gizmo gesture and check no
  unintended document, history or Apply mutation. Exercise real host
  teardown while a drag is active. Also terminate only a separate disposable
  Editor during an active drag and observe from outside that process that
  its target disappears. A fresh Editor must pick normally. Preserve
  cancellation, teardown and restored-input evidence.
- [ ] **Bounded fault checks (R1/R2).** Reject foreign, stale and wrong-format
  payloads. Exercise registration failure, reentrant/late callbacks, failed
  or redundant teardown, foreground/host loss and geometry epoch change
  through targeted tests. Check single consumption and removal of the
  hit-testable surface. These checks complement the real native gestures.

Pass requires every gate item within the Editor-only scope. Missing native
callbacks, ambiguous coordinates, lingering input interception, broken
picking/Gizmo after cancellation, or an unverified cleanup outcome stops
the run and blocks dependent work. Simulated events, the empty host alone
and an ordinary Avalonia-control result cannot confer a pass.

## Rollback

Remove the temporary target and the Editor-only COM compiler allowance.
The original 0115 paths and ordinary Viewport input remain available.
Saved scenes contain the ordinary authoring data defined by Plan 0128.
