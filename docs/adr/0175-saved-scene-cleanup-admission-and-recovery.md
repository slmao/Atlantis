# ADR 0175 — Saved Scenes Cleanup Admission and Recovery

- **Status:** Proposed
- **Date:** 2026-11-21
- **Deciders:** Maintainer
- **Acceptance:** Pending
- **Related Spec:** [Spec 0126](../specs/0126-saved-scene-cleanup-admission.md)
- **Supersedes:** [ADR-0174](0174-saved-scene-build-cleanup-ownership.md) D3 on deletion admission and recovery, upon acceptance. D1 and D2 remain in force.

## Context

The #416 Windows probe created a child after directory inspection, then observed
one deletion before the membership refusal. Another probe created a child while
the directory was open with `FileShare.None`. A same-volume rename removes the
old name from access to the selected tree. The target name and previously
granted handles still require a membership guard.

## Decision

**D1 — Reversible isolation.** Use a new, non-reparse location in the managed
attempts area on the same NTFS volume. Persist the checkout and volume identity,
source and isolation names, root ID, confirmed inventory, and a pending phase
outside the tree before the no-replace rename. Require the move to preserve the
tree's security descriptors. Reopen using the retained root identity and ordinary
components. An unexplained outcome is pending recovery.

**D2 — Exclusion precedes admission.** On the isolated tree, deny
`FILE_ADD_FILE` and `FILE_ADD_SUBDIRECTORY` through each directory's own DACL,
with no propagation or owner change. Keep no-follow handles that exclude
competing access to files and any directory handle with membership-changing
rights. Walk and enumerate relative to retained directory handles. Pin the
ancestors and retain the handles and denies until the operation is settled.
Validate that delete or rename of existing entries, creation and move-in are
fenced on Windows, including previously opened directory handles and access
through aliases. If a required handle, permission, inventory entry or
exclusion check is uncertain, refuse. The guard covers ordinary competing
processes using Windows access checks, including processes of the same user.
The launcher does not elevate privileges or change outside ACLs.

Capture the exact original DACL and restoration information per touched
directory by identity. Persist it before applying that directory's deny. Skip
unsupported security descriptors or a tree for which the complete bounded
recovery record cannot be stored. Compare under the armed guard only after
each directory has been covered. The durable phase indicating possible deletion
is written after comparison and before any disposition. Bind the first disposition
to the same live guard used for the comparison. A released or failed guard cannot
admit deletion. Continue using verified handles bottom up. A failure after
deletion begins may leave a remainder.

**D3 — Recovery follows observed identities.** Inspect only the recorded names
and established filesystem objects. Restore only known ACL states and only to a
vacant original name without replacement. A reused name or changed descriptor
retains the operation for explicit recovery. A pre-deletion conflict returns the
verified root with its ordinary additions intact when safe. After possible
deletion, restoration requires proof that the approved inventory is intact.
A partial or uncertain delete requires a fresh guarded inventory of a provable
subset of the approved tree and explicit confirmation before deleting anything
further. Unknown or replacement data remains untouched.
Clear the record only after verified restoration or verified absence of the
isolated tree following cleanup. A restart observes and reports the pending
operation, with future changes initiated explicitly.

## Consequences and Alternatives

The proposed NTFS exclusion and DACL round trip are implementation prerequisites.
If either cannot be demonstrated on the supported Windows filesystem, the PR
stays blocked. A guessed staging name, a whole-tree rescan alone, or a directory
oplock cannot enforce membership. Directory oplocks notify about membership
changes without waiting for acknowledgement. A parent-only traverse deny is
insufficient where Windows bypasses traversal checks. Transactional NTFS would
change the deletion model and Microsoft recommends alternatives.

Sources: [Windows file and directory access rights](https://learn.microsoft.com/windows/win32/fileio/file-access-rights-constants),
[directory oplocks](https://learn.microsoft.com/windows/win32/api/winioctl/ni-winioctl-fsctl_request_oplock),
[Windows traverse checking](https://learn.microsoft.com/windows/security/threat-protection/security-policy-settings/bypass-traverse-checking),
[Transactional NTFS](https://learn.microsoft.com/windows/win32/fileio/transactional-ntfs-portal).
