# ADR 0141: Authoring Node Name Storage and Runtime Projection

- **Status:** Accepted
- **Date:** 2026-11-04
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-11-04; [joint ruling](https://github.com/slmao/Atlantis/pull/323#issuecomment-6634427244), J1–J4 as recommended; PR #323 manually merged.
- **Related Spec:** [Spec 0069](../specs/0069-scene-node-names.md), R1–R3
- **Related ADRs:** Extends [ADR-0100](0100-scene-semantic-schema-layering-model-and-versioning.md) and the scene format policy in [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)/[ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md). Accepted historical text is retained.

## Context

Scene nodes already have persistent GUIDs and an Asset System authoring document;
the editor reads that document, not reconstructed Runtime components. Source7
has no label and uses space-delimited tokens. Artifact7 is a fixed runtime
projection and cannot retain an arbitrary name without a format/storage change.
Adding a reflected string would affect Core and World despite no runtime need.

## Decision

1. A name is owned, bounded authoring-node metadata, never identity, a component
   or a name-based address. Spec 0069 R1 owns its precise domain. Scene-local
   constraint data and fingerprint change with semantic version2; descriptor
   types and Core/World primitive/property vocabulary do not change.
2. Source8 adds R2's single quoted field. The reader also accepts source7 as
   unnamed nodes; the writer always emits8. This is a deliberate, bounded
   exception to the codecs' single-version precedent, not a general migration
   policy. Other unsupported versions remain errors; existing source7 files
   need no mass edit and upgrade only when explicitly saved/regenerated.
3. Names survive source/semantic/save round trips and glTF import. The existing
   runtime artifact remains format7 and explicitly projects names away. Its
   semantic declaration follows version2, whose runtime projection is defined
   to exclude that authoring-only field. Artifact-to-authoring mapping initializes
   empty names; it must never be used to replace the live authoring document.
   `cookSceneToMemory` still verifies the full source round trip before this
   projection. No runtime format/version bump or name storage is introduced.

## Consequences

### Positive

- Useful source labels without World strings, new dependencies or runtime work.
- GUIDs and cooked bytes stay stable under a name-only edit.
- Existing unnamed source7 scenes continue to load; one canonical writer.

### Negative / Trade-offs

- Source reader supports two explicitly named versions; older tools reject8.
- Cooked artifacts alone cannot recover names, as an intentional projection.
- Invalid imported names refuse rather than silently changing the source data.

## Alternatives Considered

Strict8-only migration increases unrelated edits; inline unquoted tokens cannot
represent spaces; a generic JSON parser/string schema is unnecessary. Artifact8
or a Runtime Name component pays for an unrequested consumer. Editor-side caches
lose document persistence and shared access. Revisit these only for a new use case.
