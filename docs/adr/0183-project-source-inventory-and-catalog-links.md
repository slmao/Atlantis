# ADR 0183 — Project Source Inventory and Catalog Links

- **Status:** Proposed
- **Date:** 2026-11-22
- **Deciders:** Maintainers
- **Acceptance:** Pending joint review
- **Related Spec:** [0134](../specs/0134-project-assets-explorer.md)

## Context

The 0081 catalog describes an immutable build. Project source files and
registrations can change during its lifetime. Managed import and trusted
Saved Scenes use separate authorization and activation paths. A logical
asset path can be renamed or moved independently of its source slot.

## Decision

### D1 — Keep source reads under Runtime's existing root owners

Introduce an explicit, read-only `--project-assets` opt-in on a Windows
development Runtime. The ordinary Editor may forward it to its owned
Runtime. An independent Saved Scenes Editor may receive it through the
existing checked Open launch. Apply the same shared read capability to
authenticated clients of that Runtime. Resolve the managed inbox through a
validated active 0085/0086 profile. Resolve the Prefab root from the
development Runtime's configured checkout source root using the existing
0132 ordinary-path and registration validation. The Editor's 0090 Open
flow verifies its build and checkout before forwarding the opt-in.
A Saved Scenes Runtime without live managed profile authority can browse
the checked checkout Prefab root only. Resolve no roots from working
directory, session-file contents, logical paths, or client input. A
packaged Runtime, unverified root or connection without the capability
keeps ordinary catalog access.

Pin and recheck canonical ordinary root and entry identities under the
existing Windows path rules, including containment, case aliases, hard
links and reparse points. Keep absolute paths and physical identifiers
private. A failed or uncertain read cannot establish absence or enable a
link. The new flag confers no registration, build, save or import authority.

### D2 — Use a bounded explicit shared scan

Provide `assets.scanProject` to request one read and
`assets.projectScanStatus` to observe its scoped scan ID. The request
supplies no path. Return Pending until settlement. A settled scan includes
a Complete, Incomplete or Unavailable result for each configured root,
session-scoped source keys, relative paths, closed kinds, verified
registration and link evidence, and bounded diagnostics. Bind the result
to the same frozen catalog used by `assets.list/get`, reporting catalog
unavailability independently of source read completeness. Hold one read
in flight and the latest terminal scan per Runtime. A superseded or
evicted ID has an explicit unknown result. Older hosts return unsupported
and existing clients retain their reply shapes and behavior.

Run directory and registration reads off the frame and UI threads. Reuse
the executable-private asset work pattern and the verified registration
readers. In the managed host, use the existing one-work-item admission.
A scan or write already owning that slot returns Busy to a competing
request. The checkout-only path uses one cancellable background read,
joined at shutdown. Publish owned values at the existing host dispatch
point. Coalesce overlapping view requests in the Editor. Work starts on
explicit demand. Bound traversal to four directory levels below each
root, at most 2,048 combined entries and 4,096 links, and canonical
relative paths of at most 512 UTF-8 bytes. Bound the complete result to
2 MiB. Enforce the bounds during enumeration and decoding.
Return incomplete if a limit or uncertain
validation prevents a reliable answer. Never return a truncated link set
as complete.

### D3 — Join authority and identity before exposing links

Use the same validated live registration records and the separately
verified active producer/catalog evidence. For glTF, require the exact
canonical inbox role, registered producer root GUID, and each emitted
record's canonical source ID, subkey-derived GUID and type in the current
catalog. For a base-color PNG require the role and registered GUID, exact
canonical producer key and matching typed active record. For a checkout
Prefab use the exact source unit and catalog fragment identity, GUID,
path, and valid frozen definition from `assets.prefab`. Use the same
verified frozen bytes when comparing its revision. Suppress a link where
authority, producer identity, or applicable scope completeness is
uncertain. A GUID with more than one possible source is ambiguous.
Show individually verified data from unaffected roots.

Capture an opaque evidence stamp for each link, covering the checked
source identity and registration evidence. Recheck through the same scan
on explicit navigation or handoff. Capture local mutation state and check
the relevant entries again before publishing the scan. If writes, a change
in canonical identity, or any inconsistent read invalidate the evidence,
mark the affected result stale or incomplete. Keep the two evidence
sources separate in all client replies. An active link confirms the checked
slot and registration at the time observed. Existing import admission
validates the bytes when the user explicitly submits the form.

## Consequences and alternatives

One small shared read surface supplies the same provenance to the Editor,
CLI, SDKs and MCP. The existing writer and generation contracts remain
authoritative. The checkout and inbox roots have a deliberately limited
scope and require Refresh after external changes. Scanning the local
filesystem in Editor and joining by name would duplicate path authority
and registry parsing. Replacing the catalog with a combined filesystem
index would blur the existing snapshot and activation boundary.
