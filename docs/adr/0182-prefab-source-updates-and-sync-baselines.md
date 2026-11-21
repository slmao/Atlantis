# ADR 0182 — Checked Prefab Source Updates and Instance Sync Baselines

- **Status:** Accepted
- **Date:** 2026-11-22
- **Deciders:** Maintainers
- **Acceptance:** Maintainer accepted on 2026-11-22 at `e9235765`. [Joint approval](https://github.com/slmao/Atlantis/pull/429#issuecomment-7066200488)
- **Related Spec:** [0133](../specs/0133-prefab-source-sync.md)

## Context

[ADR-0181](0181-static-prefab-registration-and-provenance.md) defines
independent static Prefab registrations and authoring-only root provenance.
The origin GUID persists, but the expanded instance can be edited freely.
The shared Prefab read describes the active build. Checkout writes and
Runtime visibility therefore have separate settlement points.

## Decision

### D1 — Preserve registration identity and pin the active source

Extend 0181's explicit Prefab write authority to updating the source of an
existing canonical registration. The Windows Runtime writer resolves the
unit by its registered identity and holds the existing checkout write lock.
Pin the ordinary NTFS parent chain and check ownership, aliases, source
identity and exact catalog fragment. Stage under a checked, private
directory in the checkout's ignored `tmp/`, on the same NTFS volume as the
target and outside the Saved Scenes input tree. The registration keeps
exactly its two leaves even if staging is interrupted. Validate the exact
staged bytes, typed dependency set and cooked candidate. Recheck the live
fragment and source against the verified old build
and the pinned registration before a single same-volume atomic file
replacement. Preserve the original fragment. Dependency edges are derived
from the source by the existing cooker and verified against the next build's
catalog. Ambiguous ownership or an observed outside write refuses the
operation. Product writers serialize under the checkout lock. External
changes discovered before replacement or during reconciliation are treated
as conflicts. This is a trusted-checkout operation.

When replacement reports an error after possible commit, classify by
read-back of the same checked unit under the lock. Exact candidate bytes
prove committed, exact old bytes prove unchanged, and any other or unreadable
state stays unknown. Clean up only positively identified private staging.
Keep the active build immutable.

### D2 — Use distinct source and instance evidence

Add `assets.prefabRevision` by GUID, returning the GUID and SHA-256 of the
exact verified frozen v8 source bytes. Retain the bytes or revision with the
same validated source, artifact, metadata and dependencies used by
`assets.prefab`. No revision can be returned for a failed definition read.
Add `authoring.updatePrefab` with the Prefab GUID, expected source revision,
donor GUID and expected document token. Capture and attest the request within
existing Authoring and Runtime ownership.

Persist at most one optional `prefabBaseline` at a linked root, using
64 lowercase hex characters. Scene authoring source v10 adds this field
alongside the existing v9 `prefab`. Readers accept v7 through v10. Emit v8
when no origin exists, v9 when origins exist and all lack baselines, and
v10 when at least one baseline exists. Older source readers refuse v10.
Unchanged cooked Scene format and projection continue to carry expanded
nodes. Save, Save As and recovery round-trip the field. Existing edit paths
preserve it or refuse operations that cannot faithfully carry it.

Compute the managed baseline in the trusted Runtime from authoritative
bounded document values, using the existing Windows SHA-256 implementation.
Frame the domain, origin asset GUID and canonical v8 bytes separately to
avoid ambiguity. The canonical scene contains the managed subtree in
relative document order with dense local document IDs, actual EntityGuids
and parent links. Set the root's outside parent, label and local TRS to
canonical empty and identity values. Preserve exact remaining values,
including optional materials, using 0132's lossless source serialization.
For template equivalence only, replace actual node GUIDs on both sides
with the same 0132 deterministic positional mapping. Include the root
renderable in both comparisons. No caller supplies a trusted baseline.
Authoring exposes typed values and its revision guard, and takes no
Windows crypto or filesystem dependency for this mechanism.

### D3 — Keep shared edits and replay atomic

Opt in to tracked instantiation through the shared guarded structural
surface. Add typed Authoring transaction modes for one instance sync and
replay. Sync supplies the expected document token, current root and frozen
source revision. Runtime resolves the verified snapshot, checks the expected
baseline or legacy equivalence, and prepares the checked Authoring
transaction. Authoring validates the same document token at commit and
validates the whole resulting scene on a copy before writing it. Use the
existing receipt and settlement model. Expose the stored baseline as an
optional node summary field and mode support as an optional document status
field, using the existing readers' tolerance of unknown fields. Publish
existing structural and component events for changed values. Baseline-only
edits advance the document revision and dirty state. Legacy clients continue
to read `assets.prefab` unchanged and can instantiate provenance-only roots on
updated hosts. Their ordinary edits preserve new metadata or refuse without
mutation. Updated clients discover a missing mode or query on older hosts
and disable the corresponding action.

Replay is bounded to one expected and one replacement subtree, each within
0132's limits. The settled edit yields the before and after values and
ordering guards for verified client-local history. Compare the current
subtree, including root placement and name, and the expected order under a
current document token. The order guard is a SHA-256 over the ordered document
node GUIDs and parent edges. An intervening structural edit can conservatively
block replay while unrelated value edits remain eligible. Reinsert exact
original or synchronized slots and identities while holding the same root
placement fixed. Validate the candidate, source association, dependency
types and GUID collisions. Verify any supplied baseline against the replay
payload. Captured expanded values suffice without retaining a previous
definition on disk.

## Consequences

- One bounded persisted baseline detects managed changes after a reopen.
  Fresh descendant GUIDs after synchronization avoid promising a durable
  mapping between historical definitions.
- Root placement and naming stay user-owned. Legacy instances need a matching
  active definition to establish a baseline.
- Source v10 requires updated authoring readers. There is no new runtime
  expansion, propagation, asset carrier, dependency or frame work.

## Alternatives considered

- **Overwrite based on the origin GUID.** The GUID cannot distinguish an
  untouched instance from user edits.
- **Store per-field overrides or a copy of every historical definition.**
  These would introduce propagation and conflict semantics beyond one
  explicit synchronization.
- **Expand or relink at Runtime load.** This would change existing saved
  scenes without a guarded authoring transaction or an Undo entry.
