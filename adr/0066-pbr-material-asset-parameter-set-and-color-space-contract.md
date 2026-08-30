# ADR 0066: PBR Material Asset — Parameter Set, Artifact Schema, and Base-Color Texture Color-Space Contract

- **Status:** Accepted
- **Date:** 2026-08-30
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-30 as part of
  [specs/0023-pbr-material-foundation.md](../specs/0023-pbr-material-foundation.md)'s
  own Human Review Approval.
- **Related Spec:** [specs/0023-pbr-material-foundation.md](../specs/0023-pbr-material-foundation.md)
  (`Approved`)
- **Acceptance Record (2026-08-30):** Accepted by Human Review as part
  of Spec 0023's own Human Review Approval (2026-08-30, commit
  `0fc6a14`). Does not change this ADR's own Decision, Consequences, or
  Alternatives Considered above.
- **Related ADR(s):**
  [ADR-0059](0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)
  (`Accepted` — extends `MaterialAssetData`'s field set and the
  artifact's schema in kind, not module boundary or ownership),
  [ADR-0057](0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)
  (`Accepted` — this ADR adds a new consumer of `SampledTextureFormat`'s
  existing `Rgba8Unorm`/`Rgba8Srgb` dichotomy, never a new format).

## Context

`MaterialAssetData` (`material_types.h:40-45`) has exactly four fields
today — `kind`, `textureAsset`, `filter`, `addressMode` — unchanged
since ADR-0059, unwidened by ADR-0061's `LitTextured` addition. No
color/metallic/roughness field exists anywhere in the chain. The
32-byte binary artifact (`material_artifact.h:14-41`) is decode-rejected
on any other size; there is no spare capacity and no dual-version
reader anywhere in this codebase's asset formats. Base-color-texture
color space today is governed entirely by ADR-0057's existing
`SampledTextureFormat` dichotomy, chosen per-texture at cook time via a
mandatory `colorSpace` parameter, never inferred — ADR-0057's own
Consequences already disclose that no validation catches an author
choosing the wrong one. Both currently-shipped Materials use
`Rgba8Unorm`; `Rgba8Srgb` has never been used by a real, shipped
Material, only by Spec 0016's own isolated test fixture. This engine
has zero alpha-blending capability for any Pipeline
(`colorBlendAttachment.blendEnable = VK_FALSE`, hardcoded,
`vulkan_device.cpp:1125`).

## Decision

**`MaterialAssetData` gains three new fields — `baseColorFactor` (RGBA),
`metallicFactor`, `roughnessFactor` — added for every `MaterialKind` via
one unconditional schema bump (no per-kind sub-schema, no dual-version
reader). A new `MaterialKind::PbrDirectLit` is the only kind that gives
them meaning. A `PbrDirectLit` Material's texture must be cooked
`Rgba8Srgb`, enforced by a new Runtime-side cross-validation.**

1. **Parameter set:**
   ```cpp
   struct MaterialAssetData {
     MaterialKind kind = MaterialKind::UnlitTextured;
     AssetId textureAsset = 0;
     MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
     MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
     float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // RGBA, linear-space
     float metallicFactor = 1.0f;                           // [0, 1]
     float roughnessFactor = 1.0f;                           // [0, 1]
   };
   ```
   No emissive, normal map, occlusion, or second texture — see the
   Related Spec's Non-Goals. Defaults are inert for non-`PbrDirectLit`
   kinds, never read by their own realization path regardless of value.
2. **Authoring grammar:** `atlantis_material_source_version: 1 → 2`,
   three new optional lines (`base_color_factor: r g b a`,
   `metallic_factor: v`, `roughness_factor: v`), absent = default. A
   version-1 source is rejected outright. Every existing `.material.txt`
   file is re-authored only to the version-2 line, matching ADR-0060's
   own scene-migration precedent — no existing file gains a new line.
3. **Binary artifact — exact 56-byte table**, confirmed a provable sum
   (no struct `memcpy`, no hidden padding — this format is hand
   little-endian shift/mask-serialized, per `material_artifact.cpp`):

   | Field | Offset | Size |
   |---|---|---|
   | magic | 0 | 8 |
   | schema version | 8 | 4 |
   | `kind` | 12 | 4 |
   | `textureAsset` | 16 | 8 |
   | `filter` | 24 | 4 |
   | `addressMode` | 28 | 4 |
   | `baseColorFactor` | 32 | 16 |
   | `metallicFactor` | 48 | 4 |
   | `roughnessFactor` | 52 | 4 |
   | **total** | — | **56** |

   Bytes 0-31 are today's real, unchanged layout. Decode rejects any
   size other than 56. **Every `MaterialKind` uses this identical
   56-byte layout** — `UnlitTextured`/`LitTextured` widen too, never a
   per-kind-length record (needed for "no dual reader" to hold at all).
4. **Metadata sidecar** widens to carry the same three fields,
   cross-validated against the artifact exactly as `kind`/`textureAsset`
   already are (`MetadataArtifactMismatch`, unchanged enumerator).
5. **Cook/decode value validation, both directions** (matching Spec
   0020's normal-length precedent): all three parameters finite and in
   `[0, 1]`, rejected at cook time (`BaseColorFactorOutOfRange`,
   `MaterialFactorOutOfRange`) and independently re-validated at load
   time against the artifact's own decoded bytes — never trusted from a
   well-formed cooker output. `-0.0f` needs no special-casing (already
   satisfies every check here and round-trips byte-identically).
6. **Base-color texture `Rgba8Srgb` requirement — `PbrDirectLit`-only.**
   Cannot run inside `cookMaterial()` (which never resolves its texture
   reference, per ADR-0059 Decision 7) — runs at Runtime's existing
   Phase 1 scene-dependency-resolution point (ADR-0060 Decision 6),
   reading an already-necessary value; fails scene load with a new
   `RuntimeInitError` sub-code (e.g. `PbrBaseColorTextureNotSrgb`) only
   when `kind == PbrDirectLit`. Every existing `UnlitTextured`/
   `LitTextured` Material and Spec 0016's own dual-format test fixture
   are unaffected.
7. **`baseColorFactor` is linear-space**, multiplying the already
   hardware-linearized sampled texel (item 6). `baseColorFactor.a` and
   the sampled texture's own alpha are both stored, validated, and
   written to the color attachment, but currently **inert** — no
   Pipeline in this engine blends. Kept RGBA (glTF-compatible shape,
   avoids a future schema bump for a Transparency spec) with this
   inertness disclosed, not claimed as functional today.
8. **`PbrDirectLit` requires a texture reference**, exactly like
   `UnlitTextured`/`LitTextured` — no texture-less variant this round
   (`realizeOneMaterialCandidate()` unconditionally realizes a texture
   for every kind today).
9. **No new error enumerator where one already fits** (ADR-0059
   Decision 8 discipline): `BadMagic`, `UnsupportedSchemaVersion`,
   `SizeMismatch` (now triggered by 56), `LogicalPathInvalid` reused
   unchanged; only items 5/6's genuinely new failure modes get new
   enumerators.

## Consequences

### Positive

- Reuses the existing cook/artifact/metadata/load shape exactly — zero
  new module, mechanism, or dependency.
- `UnlitTextured`/`LitTextured` pixels provably unaffected: their own
  realization path never reads the three new fields; neither `.slang`
  file is touched.
- Closes a real, previously-disclosed gap (ADR-0057's own "no
  validation catches this") using data that already exists.

### Negative / Trade-offs

- Every existing Material asset must be re-cooked under the new schema
  — a one-time, disclosed, zero-pixel-impact migration.
- The sRGB requirement is enforced only at Runtime scene-load time, not
  Material cook time — inherent to ADR-0059's own existing boundary.
- Three unused floats per non-PBR Material — accepted in favor of one
  unconditional schema over a per-kind artifact variant.
- `[0, 1]`-only range is a real, disclosed restriction on artistic
  intent, reversible later with real evidence.
- `baseColorFactor.a` has no observable effect today — honest cost of
  choosing the standard RGBA shape ahead of a future Transparency spec.

## Alternatives Considered

- **A per-`MaterialKind` artifact sub-format.** Rejected: every other
  Asset System format is one flat record per asset *type* — per-kind
  structural variation is new complexity this three-scalar addition
  doesn't justify.
- **A second Asset System type instead of widening `MaterialAssetData`.**
  Rejected: `MaterialKind` is already this codebase's closed-enum
  "which shading behavior" discriminator (ADR-0059 Decision 3);
  forking a second DTO/artifact/loader family duplicates it for no
  benefit ADR-0061's own `LitTextured` addition didn't need.
- **Auto-inferring texture color space from the referencing Material.**
  Rejected: contradicts ADR-0057's explicit, author-supplied
  `colorSpace` convention, and would break if two Materials of
  different kinds shared one texture `AssetId`.
- **Clamping out-of-range factors instead of rejecting them.** Rejected:
  this codebase's own established discipline (Spec 0020) rejects
  invalid authored data outright rather than silently correcting it.
