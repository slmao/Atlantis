# ADR 0176 — Local Workspace Layout Preferences

- **Status:** Proposed
- **Date:** 2026-11-21
- **Deciders:** Maintainer
- **Acceptance:** Pending
- **Related Spec:** [Spec 0127](../specs/0127-editor-workspace-layout.md)

## Context

The product Editor from [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md)
has the three-pane layout of [0093](../specs/0093-editor-visual-layout.md).
Pane sizing is user preference. A window can open a different document, use a
different checkout or lack Saved Scenes entirely. The checkout-local receipt
and history stores of ADR-0172 and ADR-0173 have separate authority and lifetime.

## Decision

Use one versioned Editor-only file per Windows user on this machine under
`%LOCALAPPDATA%/Atlantis/Editor/workspace-layout.json`, shared by ordinary
launcher and independent product Editor windows. Store only a schema version,
an optional side-width pair in DIPs and an optional closed-set tab identifier.
The fill Viewport width follows from the remaining space. Exclude explicit
measurement and forced viewport-size runs from loading and writing these
preferences. A normal window's existing temporary viewport-size override keeps
priority for its duration. The file supplies no document, tool, path or launch
authority.

Validate the complete record within a small fixed size limit on ordinary
local paths. Missing data uses the built-in defaults. Invalid, unreadable or
unsupported input uses session defaults and leaves stored bytes intact until
an explicit reset can safely replace the record. Serialize explicit changes
with a preference-local lease and atomic replacement. Read the latest complete
record under the lease, update the width pair or tab independently, and let a
successfully committed reset clear both. Across windows the last committed
explicit action for a field wins. Within a window, reset fences older pending
writes. Closing, fallback and failed retries do not publish old state. Other
open windows retain their session layout until the user acts or they restart.

## Consequences and Alternatives

The user gets one layout across projects on the machine, with tabs filtered by
the active launcher. Storage failures leave ordinary editing and launch usable.
Checkout-scoped preferences would fragment the same layout between launchers
and independent windows. Physical pixel sizes or a saved Viewport rectangle
would add monitor and window coupling. A stored tab index would change meaning
when Saved Scenes is unavailable. This design uses the existing Windows and
managed Editor surfaces.
