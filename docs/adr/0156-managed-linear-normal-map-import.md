# ADR 0156: Managed Linear Normal Map Import

- **Status:** Proposed
- **Date:** 2026-11-14
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0097](../specs/0097-linear-normal-map-import.md)

## Context

[ADR-0154](0154-managed-base-color-texture-import.md) assigns one fixed sRGB
interpretation to managed `source.png` textures and freezes producer evidence
in private manifest v4. [ADR-0155](0155-pbr-material-map-authoring.md) accepts
linear normal textures in the existing PBR material slot. A managed normal
must keep its interpretation when an admitted job is lost, when the operator
resumes from the registry, and when a fresh build cooks only committed units.

## Decision

1. Extend ADR-0154 with one managed normal form. A fixed registered PNG leaf
   and its matching logical source key determine the interpretation. Use
   `normal.png` for RGBA8 UNORM and keep `source.png` for RGBA8 sRGB. Share
   the namespace, immutable seal, GUID minting, producer and catalog type.
   Store a matching explicit color space in frozen v5 texture evidence when
   a generation contains normal units. Exact older color-only forms remain
   supported. The old build path rejects the normal registration shape.
2. Add closed normal import and status methods on the shared client surface.
   Scope status to each kind over one texture job worker and bounded history.
   Existing color clients continue seeing color receipts. The worker verifies
   the durable kind on Resume, then follows the existing commit and recovery
   rules. No material or scene edit is implicit in registration.
3. Reuse ADR-0155 for binding and GPU preview, and
   [ADR-0150](0150-local-saved-scene-build-launch.md) for the independent
   Saved Scenes build and open. Ordinary named-generation activation and
   saved-slot launch remain separate supported entry points.

## Consequences

Normal and color units can ship together with stable GUIDs and independently
reproducible cooked formats. Existing color clients keep their wire shape,
while older builders need compatible earlier project inputs. The two
status calls expose independent task views over the same finite receipts.
The new generation shape adds a version distinction at activation. The
importer trusts the operator's +Y content convention.

## Alternatives Considered

- Add a sidecar beside the old `source.png`. The existing CMake path can
  ignore an unknown extra file and silently recook the normal as sRGB.
  The new leaf and catalog key make that situation fail explicitly.
- Add optional fields to the 0095 call or return new fields in its job
  payload. Strict older codecs would lose the exact compatibility contract.
- Infer intent from the inbox filename, color tags or pixels. Those signals
  do not establish a durable tangent-space convention across clients.
- Introduce a general texture settings schema. A fixed pair of interpretations
  covers the two existing workflows with a smaller recovery surface.
