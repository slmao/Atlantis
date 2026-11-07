# ADR 0145: Authoring Asset Reference Target Types

- **Status:** Proposed
- **Date:** 2026-11-07
- **Deciders:** Pending joint human review
- **Acceptance:** Pending
- **Related Spec:** [Spec 0084](../specs/0084-asset-reference-target-types.md)
- **Related ADRs:** Extends [ADR-0099](0099-engine-schema-core-and-descriptor-vocabulary.md) and the authoring schema projection in [ADR-0130 D3](0130-authoring-access-interface-and-module-boundary.md). Reuses the cooked asset vocabulary in [ADR-0143](0143-shared-cooked-asset-catalog-queries.md) and the recovery boundary in [ADR-0144](0144-correlated-authoring-apply-outcomes.md).

## Context

Authoring descriptors identify an asset reference without saying which cooked
type it expects. The catalog describes the actual type of each record.
Publishing the expected type allows shared clients to compare the two before
submitting a choice. The Runtime already checks the actual asset graph when it
prepares a reload.

## Decision

1. The module that owns the authoring scene field descriptor declares its
   target type. Append a default-empty `std::string_view targetAssetType` to
   Core's descriptive field record. Core enforces structural applicability
   without owning an asset taxonomy. Asset System declares the closed cooked
   catalog tokens on its semantic scene descriptors. Clients use the
   declaration carried by the descriptor, with owned native remote schema
   storage or canonical static tokens for decoded views.
2. Carry the optional token in the existing authoring schema field object
   with additive member compatibility. An omitted member represents
   unspecified metadata. A present invalid declaration follows normal
   malformed-schema handling. Keep the semantic scene hash and field/type
   identity stable because the annotation describes selection intent and
   does not change the serialized value or its authoring domain.
3. Apply the metadata to candidate selection, with no new catalog validation
   in document transactions. Runtime's existing resolution rules govern
   the complete scene and remain independently testable. Spec 0084 owns the
   exact published fields, client exposure and chooser rules.

## Consequences

### Positive

- All clients inspect a single declaration while the catalog supplies actual
  candidate types.
- Old clients and saved scenes remain usable. Updated clients can distinguish
  an absent declaration from an invalid schema response.

### Negative / Trade-offs

- Native descriptor consumers rebuild, and the wire codecs must agree on the
  optional member. Adding a cooked type later needs an explicit compatibility
  decision because updated readers validate the closed vocabulary.
- Runtime already expresses dependency expectations in its loader. Tests
  must detect divergence from the descriptive annotations while both exist.
- A filtered candidate can still fail during reload. The existing apply
  outcome and recovery flow continues to expose that result.

## Alternatives Considered

- **Infer types in the Inspector from names or current values.** A client
  heuristic would vary across clients and fail for absent or incorrect values.
- **Publish a separate target registry or discovery RPC.** Adds a second
  identity and lifetime to information owned by the field descriptor.
- **Require catalog checks at authoring transaction time.** Changes the
  document contract and introduces catalog availability into edits and history
  replay. A type check still cannot establish loadability of the scene.
