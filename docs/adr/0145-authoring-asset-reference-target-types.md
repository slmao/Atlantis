# ADR 0145: Authoring Asset Reference Target Types

- **Status:** Accepted
- **Date:** 2026-11-07
- **Deciders:** slmao
- **Acceptance:** 2026-11-07, [joint ruling](https://github.com/slmao/Atlantis/pull/331#issuecomment-6708205947) on `d58ea8d`. Design #331 manually merged as `065cdf0`.
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
   Core's descriptive field record. Core checks whether a declaration can
   appear on the field's shape. Asset System supplies the canonical cooked
   token on its semantic scene descriptors. Reuse the catalog vocabulary
   when decoding the annotation in each typed client. Native decoded token
   views follow the existing schema lifetime through owned schema storage
   or canonical static storage.
2. Carry the optional token in the existing authoring schema field object
   with additive member compatibility. An omitted member represents
   unspecified metadata. A present invalid declaration follows normal
   malformed-schema handling. Only omission permits fallback. Keep the
   semantic scene hash and field/type identity stable because the annotation
   describes selection intent without changing the serialized value or its
   authoring domain.
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
