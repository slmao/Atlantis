# Spec: Authoring Apply Outcomes and Recovery

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-07
- **Related Plan(s):** [Plan 0083](../plans/0083-authoring-apply-outcomes.md)
- **Approval:** slmao, 2026-11-07; [joint ruling](https://github.com/slmao/Atlantis/pull/329#issuecomment-6702522226) on `1a65902`; #329 manually merged
- **Related ADR(s):** [ADR-0144 — Proposed](../adr/0144-correlated-authoring-apply-outcomes.md)

## Summary

Give an accepted authoring apply an identity and a queryable terminal result,
so every client can distinguish acceptance from a successful Runtime scene swap,
a failed reload and replacement by a newer apply. Preserve the authoring
document and its history when reload fails; make recovery an explicit operation
over the same shared contract used by Editor App, CLI, both SDKs and MCP.

## Motivation / Problem Statement

Baseline: main `2690d91907682155ac7ed97e465bae65c1af805a`, the human merge of
[#328](https://github.com/slmao/Atlantis/pull/328), 2026-11-07. That PR disclosed
a pre-existing boundary: `AuthoringSession::apply()` accepts a cooked/decoded
document, `RuntimeApplication::performPendingSceneReload()` can subsequently
refuse it (for example `DependencyTypeMismatch`), and `AuthoringHost::afterFrame()`
reports only a changed generation. The old World continues; no `Applied` event
arrives. Editor App's ApplyController and CLI's apply waiter can wait indefinitely.
An unrelated client's successful apply can also be mistaken for their completion.

[ADR-0131 D3](../adr/0131-authoring-apply-and-runtime-scene-reload.md) deliberately
made failed reloads diagnostic-only. Fixing this requires a reviewed shared
contract, not an editor timeout or treating a Runtime diagnostic as authoring
`ApplyError`. #328's retained failure is motivation, not verification of this
new behavior.

## Goals

- Correlate an accepted snapshot with its eventual outcome across clients.
- End reload-failure waits without reverting an already committed document edit.
- Specify bounded observation, latest-wins applies, pause and explicit recovery.
- Preserve existing client entry points and the all-or-nothing scene swap.

## Non-Goals

No live/incremental projection, asset-type metadata or compatibility validation,
asset builds/imports/catalog refresh, automatic rollback, persistent jobs or
cross-process recovery journal, cancellation API, new transport/protocol version,
World command/event, renderer feature, dependency, thread or scheduler. No full
native authoring SDK CRUD layer or new MCP composition. Crash/device-loss
recovery and durable unsaved documents remain outside scope.

## Requirements

### Functional

**R1 — Acceptance identifies a snapshot.** Add `applyTracked()` to the shared
AuthoringConnection, returning `Result<ApplyReceipt, ApplyError>`. Existing
acceptance refusals (`NoDocument`, `NoSceneSink`, `CookFailed`, `DecodeFailed`,
`ReloadRefused`) remain synchronous, create no accepted record and do not
supersede an earlier pending apply. A receipt contains:

| Value | Meaning |
|---|---|
| `id: ApplyId` | Server-minted opaque non-nil 128-bit identity, canonical lower-case GUID text on the wire; a distinct value type, not an AssetGuid, EntityGuid, command ticket or scene generation. Never reused, including after restart. Reuse the existing GUID mint implementation. |
| `documentRevision: uint64` | The session's revision captured by this apply's cook/decode; observational identity, not a CAS precondition or a saved-file revision. |

The session revision advances on each successful open and each accepted
nonempty document transaction, including an equal-value transaction; failed
operations and empty transactions do not advance it. It does not advance for
save, apply or Runtime World commands. Reopen advances it even for identical
source bytes. Revision identity is session-local; ApplyId disambiguates restarts.
The accepted immutable scene snapshot is independent of subsequent document
edits/open/save. Neither another client's edits nor the caller disconnecting
changes/cancels it. Acceptance does not promise a swap or a rendered/presented frame.

**R2 — One pending reload, explicit terminal results.** Keep the frame-thread
and existing swap point, after old World commands and held by pause. Each
accepted request starts Pending; when swapped, refused, replaced or orderly
stopped, it reaches exactly one of the following terminal states. A pause may
hold Pending indefinitely; a crash/undeliverable outcome remains externally
unknown, not a synthetic terminal result.

| State | Required result | Effect on Runtime |
|---|---|---|
| `Applied` | The request's new `generation` | Scene/World/access/resources swap committed; generation increments once. Legacy `Applied{generation}` still emitted. |
| `Failed` | Closed `ReloadError` | No scene swap or generation increment; old World continues on recoverable failures. |
| `Superseded` | `supersededBy: ApplyId` | A newer accepted request replaced it before the swap; this request will not execute. |

Pending carries none of those terminal payloads. Terminal payloads are immutable;
duplicate, stale or mismatched host reports cannot settle another request or
change a terminal result. A successful later frame/present is not part of the
Applied guarantee; a later unrelated failure cannot rewrite Applied to Failed.

All clients share the existing latest-pending-wins slot. A newer accepted apply
terminalizes the replaced request as Superseded, even if the revisions are equal;
a refused newer apply leaves it pending. A paused request may remain Pending
until Step/Resume or replacement; no elapsed-time failure is inferred. Serial
frame execution introduces no parallel executing queue or automatic retry.

Acceptance is one serialized operation: only after B's cook/decode/sink
acceptance succeeds, register B Pending and terminalize displaced A as
Superseded by B, before any caller can observe B's acceptance. Do not defer
those supersessions to afterFrame or overwrite them in a single last-outcome
slot: A→B→C in one request batch must preserve both A→B and B→C records, even
without another frame. The sink returns displaced identity as acceptance
metadata; it must not re-enter Authoring with a completion before B is registered.
The frame-thread bridge then needs at most one unforwarded executed-reload
outcome, consumed before another request batch. Update Applied/projection before
publishing legacy Applied. This adds neither an unbounded report queue nor a
reporting dependency from RuntimeHost to Authoring.

Keep only the newest pending scene snapshot. Release a displaced snapshot on
successful replacement; after failure/swap release its pending input and
temporary preparation state through the existing cleanup path. Result records
own metadata only; queries, reopen and disconnect do not pin old scene copies.

`ReloadError` is Connection-owned: `ScenePreparationFailed` (every existing
loadSceneForReload error, including catalog/dependency/schema/type/color-space
and mesh upload), `RenderPreparationFailed` (required bloom configuration/targets),
or `RuntimeStopped` (fatal swap preparation/shutdown). These deliberately coarse
categories map the existing return boundaries, without expanding the loader's
error API. The Runtime keeps detailed diagnostics tagged with ApplyId and
reports every existing reload exit. Runtime/OS/GPU error types do not cross
the boundary. The current bake over validated data has no recoverable error;
do not invent a bake-failure path or catch programmer errors. Fatal `waitIdle`
failure retains the existing fatal lifecycle;
report RuntimeStopped before teardown where execution permits, but never promise
a continuing usable World/device or delivery after a crash/disconnect.

**R3 — Non-destructive, bounded observation.** Add
`applyStatus(optional<ApplyId>)`, returning a value snapshot or `UnknownApply`.
No id selects the newest accepted record, or none before any acceptance; an
explicit id selects that retained record (including the projection exception
below). The snapshot contains:

- `currentDocumentRevision`, null before a successful open;
- `request`, null only for an id-less query before any accepted apply; otherwise
  its receipt, state and the state-specific payload from R2;
- `projection`, null until a tracked authoring swap, otherwise the last successful
  receipt plus its generation, independently retained from the request history.

Queries never drain, acknowledge or trigger work and are available before open.
Records are shared across connections, not private failure queues. Retain the
one pending record and the latest **64 terminal records in terminalization
order**, across document opens and client disconnects, for the AuthoringSession's
lifetime. Evict the oldest terminal record only; preserve projection metadata.
If the explicit id is the last successful projection's id, return its immutable
Applied record even after it leaves the 64-record window; the independently
retained receipt/generation already supplies that result. This is one bounded
exception, not a second history. Unknown/otherwise-evicted/foreign ids return
UnknownApply, never Pending or an invented historical result. The maximum is
one pending record, 64 terminal entries and one projection metadata entry;
entries may refer to the same id.
Restart loses the volatile records; old ids cannot match new work. Transport
loss, a lost acceptance reply, unsupported method and UnknownApply mean the
client lacks a known result, not that the Runtime failed or succeeded. No blind
resubmission or idempotent replay guarantee; re-query a known id on reconnection
to the same Runtime, otherwise require an explicit new apply after inspection.
An id-less query is global discovery, not "my last request": after a lost reply,
do not adopt its newest id or a matching revision as the caller's receipt.
Revisions/generations alone cannot reconstruct request ownership. Superseded's
successor id remains informational if that successor is later evicted; do not
pin an unbounded successor chain. Following it does not change the old terminal.

Projection identifies the origin of the current World, not byte equality with
the current document: gameplay writes/previews may have changed that World.
After failure or supersession it stays at the last successful swap. Revision
differences show document changes not included in that projection; matching
revisions do not rule out disposable World changes.

**R4 — Uniform clients and compatibility.** Remote adds only
`authoring.applyTracked` (no params) and `authoring.applyStatus`
(`id`, optional canonical ApplyId; absent/null selects newest); both carry
exactly R1–R3 over `atlantis.remote/1`. Tracked apply returns `{"ok":<receipt>}`
or `{"err":"<ApplyError>"}`; status returns `{"ok":<snapshot>}` or
`{"err":"UnknownApply"}`. Receipt is `{id,documentRevision}`;
snapshot is `{currentDocumentRevision,request,projection}`. A request is
`{receipt,state}` plus only its state-specific `generation`, `error` or
`supersededBy` field; projection is `{receipt,generation}`. Absences in the
snapshot use null. Malformed/nil ids use the existing RPC BadRequest; a valid
unknown id is the in-band UnknownApply. Keep `authoring.apply` and its `{"ok":null}` / ApplyError
shape, and the existing authoring event/filter spellings. Legacy apply uses the
same tracked engine path internally and remains acceptance-only; no new outcome
event is needed. Existing subscribed success observers continue to work, but
old clients gain no failure guarantee until upgraded. No changes to command
failure draining or transaction settlement, `control.status` or asset queries.

A Failed/Superseded record is a successful status query, not an ApplyError or
transport failure. Clients inspect its state; tool/RPC success alone never
means that the scene was applied.
Native Remote callers check session failure before interpreting any sentinel
return; a broken transport must not be classified as UnknownApply or a reload
refusal. Managed callers retain the existing transport/protocol exception rules.

| Client | Required exposure/use |
|---|---|
| Native Connection / Remote | The two typed operations, owned values and identical error/absence semantics, frame-thread/caller-thread only as today. |
| Native Gameplay SDK | A narrow inline `AuthoringApply` facade over an injected AuthoringConnection, with no Runtime dependency or unrelated authoring API. |
| C# SDK | Typed receipt/status values and `Authoring.ApplyTracked/ApplyStatus`; existing Apply remains compatible. |
| CLI | `scene apply` uses tracked acceptance and waits only on its id; success reports id/revision/generation, failure or supersession reports a nonzero result. While paused return an explicit pending receipt immediately (exit 0, not an applied claim); preserve deferred reporting with the same id. Add nonblocking `scene apply-status [id]` for later inspection/reconnection, printing Pending/Applied/no-record with exit 0 and Failed/Superseded/UnknownApply with nonzero. |
| MCP | Two exact tools `authoring.apply_tracked` / `authoring.apply_status` over the SDK; keep legacy `authoring.apply` and document acceptance versus completion. Existing explicit `authoring.drain_failures` before applying remains mandatory. No hidden waits/retries/Step/Resume or log parsing. |
| Editor App | Use receipt/status on the existing worker; legacy Applied may prompt refresh but cannot settle a tracked apply. |

An unpaused CLI wait may still encounter transport loss or an operator deadline;
report unknown completion with the receipt, never success or ReloadError by
timeout. Retain the id in timeout/transport error output. A continuing invocation
polls its deferred receipt at existing per-command/finish opportunities, without
relying on events or a Step command; no idle-stdin notification loop is added. Before
replacing its local deferred waiter with a new accepted apply, query/report the
old receipt's state (including UnknownApply) rather than silently dropping it.
Keep only that local waiter; the shared bounded store supplies older inspection.
A pending acknowledgement from an exited process has no retrospective exit code;
a continuing invocation emits observed deferred failure/supersession and
aggregates it into a nonzero exit result, rather than printing an error but
returning success. Each emitted reply keeps its own command/id; an older failure
must not be attributed to the newly accepted request.

An upgraded client against an old Runtime receives the existing BadRequest /
protocol-session failure. Without negotiation BadRequest may also mean malformed
params: report method/detail, not a proven "unsupported" or ReloadError verdict.
Preserve any committed document edits and do not silently fall back to
generation-only completion; session-end history rules still apply. Editor App
first probes the read-only id-less applyStatus on the existing session before
enabling document-changing operations or issuing its automatic initial open.
Failure prevents new local edits/history, not a trial apply or a new handshake;
repeat readiness for each new session, not on every edit. Generic SDK/CLI/MCP
calls retain their existing failure semantics and perform no hidden probe/mutation.
Existing binaries' legacy RPCs remain compatible;
no native ABI compatibility promise is introduced for rebuilt C++ interfaces.

**R5 — Editor settlement and recovery.** A matching Applied result releases the
local in-flight receipt and may issue one coalesced apply of the newest document
for already queued local commits. Another id's result/generation must not do so.
Guard worker callbacks by session, document-open lifetime, receipt and poll
sequence; a late Pending response cannot replace an observed terminal result.
While a known receipt is Pending, query its status in ordinary worker polls
even if authoring drainEvents is empty or Runtime is paused; failure has no new
event. Process available reopen/edit events first so the query result cannot
settle an older document-open lifetime. No extra scheduler or wait on the UI thread.
Failed, Superseded or synchronous refusal releases the waiter, discards the
automatic coalesced retry latch and suspends automatic applies; show the reason,
document revision and last projection explicitly. Unobservable completion
likewise suspends automatic submission while retaining the unresolved receipt.
Do not loop automatically on new edits, an event, a timer or reconnection.

During suspension all document mutations, including history replay and reference
confirmation, use submit/transaction plus explicit failure settlement only; they
must not call the unconditional CommitAndApply pipeline. Normal unsuspended
editing may keep its existing pipelined submit/drain/tracked-apply optimization,
including its unchanged-document apply after a refused transaction. Track such
a receipt separately: an accepted apply never turns a refused transaction into
a successful edit/history record. Conversely, synchronous apply refusal or later
reload failure cannot erase a successfully settled transaction's history. No
edit/history step waits for Runtime completion.

Ending recovery needs more than historical request success. Clear its preview
restriction only when the last projection has the matching Applied receipt's id
and generation, and its revision equals the observed current document revision.
If local commits
already queued a coalesced follow-up, keep the restriction through that follow-up;
if an external edit/new open made the document newer, or another request moved
the projection, show that the document/projection changed and remain suspended
until Apply current document instead of auto-applying another client's work.
A historical Applied record alone cannot label the current World
or document as that result. These checks cover observed state, not a new atomic
CAS guarantee against changes after the query.

The recovery rules are:

| Action/state | Document and history | Running scene / apply state |
|---|---|---|
| Recoverable reload failure | Keep all committed edits and their normal undo/redo records; no synthetic undo or history clearing. | Keep old World, including disposable writes/any already submitted preview; do not label it the failed document. |
| Edit, choose a corrected reference, Undo/Redo while suspended | Ordinary guarded transactions/history/conflict rules continue. Refused transactions remain refused; a failed reload does not undo a successful transaction. | No automatic apply until explicit recovery. |
| **Apply current document / Retry** | Snapshot the latest document once; no new history entry. | New receipt, never replay of a failed cooked snapshot. Acceptance resumes ordinary pending/coalescing; another failure suspends again. Disable duplicate recovery while this local receipt is Pending. Explicit recovery may replace another client's pending apply under R2. |
| Save | Existing atomic hand-authored save; imported save refused. Does not prove Runtime compatibility. | No apply, no result reset or retry. |
| **Reopen from source (shared open)** | On success explicitly discards unsaved state, emits DocumentReopened, clears history and advances revision. A refused open leaves document/revision/history and recovery mode unchanged. | No automatic apply or cancellation of an already accepted snapshot. Explain any old pending apply may still swap; remain suspended after successful reopen until Apply current document. Old-revision callbacks cannot clear this recovery state/history or claim the reopened document was applied. |
| Disconnect / restart / unknown result | Retain no false success; existing session-end history clearing and unsaved-document lifetime rules apply. | Explicit status refresh if the same live Runtime/known id is available. Otherwise inspect and explicitly apply the current opened document; no durable job recovery promise. |

The new editor control is Apply current document / Retry with its result display.
Reopen remains the existing shared open operation (for example through CLI/SDK/MCP),
which the editor observes; no new Reopen button, file picker or confirmation
workflow is required in this slice.

Until a successful projection for the current document is observed, a suspended
editor disables new Gizmo previews, whose World/document basis may differ;
ordinary document Inspector/Hierarchy/reference editing and viewport viewing
remain available. Entering recovery also invalidates any uncommitted active
gesture and its unsent preview/release intents, so a late pointer release cannot
commit it. Do not invoke the normal cancel's start-matrix rollback against a
possibly replaced World; abandon this local gesture without extra World writes.
Already committed gestures retain their history. Already queued previews/World
writes that were sent before invalidation are not retroactively
cancelled, and no new World rollback channel is added. On acceptance of a
recovery request keep that preview restriction until a matching successful
current projection (including any coalesced follow-up); another client's result
may be displayed but cannot clear the local recovery latch. Do not relabel a newer revision
as rendered on an older result. Normal play/pause/Step/Resume remain explicit
controls. One Step executes the currently accepted snapshot; if its completion
issues a coalesced follow-up while still paused, that request needs another
Step or Resume. Fixing an asset reference or retrying is not recooking assets or
refreshing Spec 0081's fixed discovery snapshot.

RuntimeStopped requires a new usable Runtime/session before retry; the editor
does not offer an in-place device recovery. An unknown result releases a busy
indicator into an explicit unknown state, not a terminal verdict: allow status
refresh or, after inspecting current state, an explicit new apply with a warning
that an older accepted request may still take effect.

### Non-functional

**R6 — Boundaries and delivery.** Connection owns the shared values, Authoring
owns revisions/records, Runtime owns actual reload outcomes, and the executable's
AuthoringHost bridges them. Retain single frame-thread orchestration, the one
pending snapshot, all-or-nothing swap, existing resource cleanup and no World
operation additions. Only bounded metadata is retained, not 64 scene copies.
Query from the existing editor worker/poll cycle; no new subscription, scheduler,
thread, NuGet/native dependency, asset format or generated World schema change.
Windows is the Runtime/editor acceptance platform; changed native libraries
must still compile for Android, which continues to host no authoring session.

## Proposed Design

Use the existing scene-sink handoff with ApplyId carried alongside its immutable
scene. AuthoringSession commits acceptance and any supersession from the sink's
acceptance metadata in R2's one serialized operation; the executing reload
reports a typed success/failure with that same identity. AuthoringHost
forwards the outcome, rather than inferring completion solely from generation.
Existing legacy success publication remains. Polling a retained record supplies
completion even without a subscription or after another client drained events.
This is a small volatile result store, not a general job framework.

## Architectural Impact

**Yes:** two public AuthoringConnection/wire/SDK operations and result ownership;
the scene-sink outcome handoff; Editor App's failed-apply recovery policy.
[ADR-0144](../adr/0144-correlated-authoring-apply-outcomes.md) is required and must
be Accepted in joint review. It partially supersedes ADR-0130's closed session
surface, ADR-0131 D3's diagnostic-only failure report, ADR-0106 D3's closed wire
surface as subsequently extended, and ADR-0135 D3/D5 for failed-apply recovery.
Keep document ownership (ADR-0129), atomic reload/World separation (ADR-0131),
ordinary-client access (ADR-0125) and history/conflict rules (ADR-0136).
Accepted ADRs and approved historical Specs are not rewritten to match the proposal.

## Alternatives Considered

- Add only a failure event or parse logs: no reliable per-request association or
  late/reconnecting observer result; a global generation can settle the wrong apply.
- Change legacy apply's result shape or add an outcome event: unnecessary client
  compatibility/filter churn when an additive receipt/query suffices.
- Serialize/refuse all competing applies: changes established latest-wins behavior.
- Restore document/history or automatically retry: conflates accepted edits with
  projection failure, risks losing work or repeating a deterministic refusal.
- Persist every outcome/cooked snapshot or add cancellation: expands into job and
  recovery storage beyond this feedback gap.

## Testing & Verification Plan

[Plan 0083](../plans/0083-authoring-apply-outcomes.md#verification-checklist) maps
R1–R6 to lifecycle/codec/model tests, real shared-client and Windows recovery
checks, retained renderer/golden obligations and the selected performance gate.
Design-only delivery runs document/link/scope checks; none of the proposed
implementation behavior is claimed implemented or tested in this PR.

## Risks & Open Questions

Joint review must decide the Plan's J1–J4 recommendations: additive polling and
identity, retention/latest-wins, explicit editor recovery (including reopen and
preview limits), and cross-client delivery/verification. Outside the independently
retained projection, a query after eviction cannot reconstruct an old terminal
result. Old binaries retain their
diagnostic-only failure limitation. Polling has the existing worker cadence,
not a promised real-time notification latency. Fatal process/device failure
can prevent any reply; this design must not turn that uncertainty into a verdict.

## Out of Scope / Future Work

General capability negotiation beyond R4's readiness check, durable/replayable operations,
last-good document restore, asset compatibility metadata and incremental scene
projection require separate scope. This proposal does not close those gaps.
