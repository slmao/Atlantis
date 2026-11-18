# ADR 0167: Replace a Managed PNG With Its Existing Identity

- **Status:** Proposed
- **Date:** 2026-11-19
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0116](../specs/0116-registered-texture-reimport.md)

## Context

[ADR-0154](0154-managed-base-color-texture-import.md) makes sealed texture
units and the immutable activated generation authoritative for different
parts of the managed workflow. Register commits a new unit with one
no-replace rename. Updating an existing unit changes both the PNG and its
seal. Windows cannot atomically replace a nonempty unit directory; two
uncoordinated file writes could pair a new PNG with an old seal. Publishing
a generation is a separate same-volume rename.

## Decision

1. Keep the registered texture GUID, exact fragment, logical key and role.
   Only the fixed original inbox path is user-editable. Build and validate
   a private replacement through the existing registered producer, with
   a staging selector distinct from registration. Maintain the existing
   project/staging locks and persistent dirty markers; exclude other
   managed writes. This remains a trusted local workflow: do not run
   independent configure/build against the managed project while the
   exchange or repair is in progress.
2. After validating the whole candidate, prepare the existing-format
   generation. Under the publication commit guard, revalidate the old
   registry and prepared candidate. Record one private recovery record
   with job ID, expected generation ID, target identity and old/new
   digests, and retain the two verified units under the job's owned paths
   as needed. Record/flush the recovery information before the first
   rename. Move the old registered unit to a same-volume private backup,
   then move the verified replacement into the now-vacant slot. Use
   no-replace renames; inspect the outcome and verify the registered unit
   before starting publication. Recheck the frozen candidate immediately
   before the publication rename. The exchange is not a single atomic
   filesystem action. Timeout or shutdown winning before the first rename
   prevents the exchange; after it starts, settle or retain repair evidence. No consumer of an active generation
   reads the mutable registration slot.
3. The existing named-generation rename remains the publication commit
   point. Confirm the intended final directory by the same frozen checks
   when the rename result is uncertain. Roll back only when the absence
   of this publication and the old backup and current slot state are
   verified, and restoration can be verified. Never delete an
   unrelated destination or treat a damaged directory as completed. Keep
   the original inbox source for a new explicit attempt. Cleanup failure
   after a verified publish retains success and the repair hold.
4. On interruption or ambiguous ownership, retain the records and locks'
   existing dirty-state refusal semantics across restart. Offline, stop
   writers and children, and verify identity, digests and the expected
   destination: keep the new unit only with its validated complete
   generation; restore the verified backup if publication definitely did
   not occur. Otherwise leave the workspace blocked for repair instead of
   guessing. Clear markers only after proving registry and staging are
   consistent. The prior complete generation remains selectable
   read-only. This is not the Register Resume path.
5. Use the existing shared rebuild request, job and status shapes. The
   new behavior is eligibility for one producer under an explicit
   request on the trusted profile. No catalog/manifest or wire version
   changes. This ADR narrowly extends ADR-0154's immutability rule for
   host-owned replacement; the ordinary seal and frozen generation checks
   remain in force.

## Consequences

A clean input failure cannot replace the previous version or change any
bindings; an uncertain source exchange blocks managed writes and may
require local repair. The temporary backup adds bounded per-texture I/O
and space. Runtime activation and old generation use remain independent
of the mutable registry. The shared client operations and readback
interfaces need no new shapes.

## Alternatives considered

- Reuse Register with a new GUID. This requires rebinding existing
  materials and fails the existing-identity requirement.
- Replace PNG and seal directly. It cannot establish a consistent
  recoverable unit across two writes and bypasses the frozen input checks.
- Keep the registry old and publish only a new generation. The selected
  generation would disagree with the project on future builds or edits.
- Introduce a content-addressed registry or a new public transaction
  protocol. A bounded local replacement and recovery record is enough for
  the one-source first slice.
