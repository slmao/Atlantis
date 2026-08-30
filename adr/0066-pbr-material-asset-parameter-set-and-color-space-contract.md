# ADR 0066: PBR Material Asset — Parameter Set, Artifact Schema, and Base-Color Texture Color-Space Contract

- **Status:** Proposed
- **Date:** 2026-08-30
- **Deciders:** slmao — pending Human Review as part of
  [specs/0023-pbr-material-foundation.md](../specs/0023-pbr-material-foundation.md)'s
  own Human Review Approval.
- **Related Spec:** [specs/0023-pbr-material-foundation.md](../specs/0023-pbr-material-foundation.md)
  (`In Review`)
- **Related ADR(s):**
  [ADR-0059](0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)
  (`Accepted` — this ADR extends `MaterialAssetData`'s own field set and
  the material artifact's own schema in kind, not in module boundary or
  ownership model; the Core-only `Atlantis::AssetSystem` boundary ADR-0059
  established is unchanged),
  [ADR-0057](0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)
  (`Accepted` — this ADR's own base-color texture color-space requirement
  reuses `SampledTextureFormat`'s existing `Rgba8Unorm`/`Rgba8Srgb`
  dichotomy unmodified; it adds a new cross-validation consumer of the
  existing, unmodified texture metadata `colorSpace` field, not a new
  format or a new decode path),
  [ADR-0061](0061-world-light-component-and-scene-lighting-binding-boundary.md)
  (`Accepted` — precedent for adding a `MaterialKind` enumerator that
  reuses `MaterialAssetData`'s own shape rather than forking a new DTO).

## Context

Confirmed directly against real, current source (`main` at `f7c2d18`, the
commit Spec 0023's own drafting branched from):

- `MaterialAssetData` (`src/asset_system/include/atlantis/asset_system/material_types.h:40-45`)
  has exactly four fields today — `kind`, `textureAsset` (an `AssetId`),
  `filter`, `addressMode` — unchanged since ADR-0059 first introduced it
  and unchanged by ADR-0061's later addition of the `LitTextured`
  enumerator (that addition reused this exact same four-field shape,
  per ADR-0061 Decision 3 and this file's own comment at lines 26-29).
  **No color, tint, metallic, or roughness field exists anywhere in this
  chain** — confirmed absent from the authoring source grammar
  (`src/asset_system/include/atlantis/asset_system/material_source.h:16-21`,
  `src/asset_system/src/material_source.cpp:10-14`), the binary artifact
  (`src/asset_system/include/atlantis/asset_system/material_artifact.h:14-41`,
  a fixed 32-byte record), and the metadata sidecar
  (`src/asset_system/include/atlantis/asset_system/material_metadata.h:19-24`).
- The material artifact's own binary layout is a closed, fixed-size,
  32-byte record (`kMaterialArtifactHeaderSizeBytes = 32`,
  `material_artifact.h:34`), decode-rejected outright on any other size
  (`src/asset_system/src/material_artifact.cpp:82-87`,
  `UnexpectedSize`) — there is no spare capacity and no dual-version
  reader anywhere in this chain, matching every other Asset System
  format's own established convention (mesh, scene, texture).
- The material authoring grammar is a fixed, five-line text format
  (version line + `kind`/`texture`/`filter`/`address_mode`), parsed by
  fixed-prefix line matching
  (`src/asset_system/src/material_source.cpp:53-101`) — adding a field
  means adding a new fixed-prefix line, following the exact same
  mechanical pattern ADR-0060 already used to add `material=` to the
  scene source grammar.
- Base-color-texture color space today is governed entirely by
  ADR-0057's existing, unmodified `SampledTextureFormat` dichotomy
  (`Rgba8Unorm`/`Rgba8Srgb`), chosen per-texture at cook time via a
  mandatory, explicit `colorSpace` cooker parameter — never inferred.
  ADR-0057's own Consequences (`adr/0057-...md:209-215`) already
  discloses, in plain terms, that **no validation catches an author
  choosing the wrong `colorSpace` for a given texture** — "this ADR
  introduces no automatic detection." Both currently-shipped Material
  assets (`assets/materials/unlit_textured_quad.material.txt`,
  `assets/materials/lit_textured_quad.material.txt`) reference the same
  texture, `textures/textured_quad_source_unorm.png`, cooked as
  `Rgba8Unorm` (confirmed by its own logical-path naming and by
  `Rgba8Srgb` never being exercised by any real, shipped Material asset
  today — only by Spec 0016's own isolated color-space GPU test
  fixture). **Adopting `Rgba8Srgb` for a real, production PBR base-color
  texture is a genuine first production use of that existing, already-
  `Accepted` code path, not new machinery.**
- Physically-based, metallic-roughness-workflow PBR (the model this ADR's
  own Related Spec adopts) is, by long-established, widely-documented
  convention (glTF 2.0's own metallic-roughness material model; Disney's
  "principled" BRDF papers; Karis, "Real Shading in Unreal Engine 4"),
  defined so that a base-color *texture*'s texel values represent
  sRGB-encoded reflectance and must be decoded to linear light before
  any lighting math consumes them, while a base-color *factor* (a
  shader-side multiplier with no texture backing it) is conventionally
  authored and consumed directly in linear space. This is a real,
  external, well-established convention this ADR adopts by reference,
  not an invention of this codebase.

The real design question this ADR settles: what minimal parameter set
does a physically-based, metallic-roughness Material need; how does that
parameter set travel through the existing cook/artifact/metadata/load
chain without breaking any existing Material asset's own bytes or any
existing `MaterialKind`'s own rendered pixels; and how is the base-color
texture's color-space requirement enforced, if at all, given ADR-0057's
own disclosed absence of any such check today.

## Decision

**`MaterialAssetData` gains three new fields — `baseColorFactor` (RGBA),
`metallicFactor`, `roughnessFactor` — added for every `MaterialKind`,
not only the new one, via a single, unconditional schema version bump
across the whole Material asset format (no per-`MaterialKind` sub-schema,
no dual-version reader). A new `MaterialKind::PbrDirectLit` enumerator is
the only kind that gives these three fields real rendering meaning;
`UnlitTextured`/`LitTextured` continue to ignore them exactly as they
already ignore every field their own shader pair does not reference. A
`PbrDirectLit` Material's own referenced texture must be cooked as
`SampledTextureFormat::Rgba8Srgb` — enforced by a new, explicit
cook/load-time cross-validation this ADR introduces, closing the exact
gap ADR-0057 already disclosed and left open.**

1. **Parameter set — exactly three new scalar-shaped fields, no more:**
   ```cpp
   // atlantis::asset_system, material_types.h -- extends the existing,
   // unmodified four fields; kind/textureAsset/filter/addressMode are
   // untouched in name, type, and meaning.
   struct MaterialAssetData {
     MaterialKind kind = MaterialKind::UnlitTextured;
     AssetId textureAsset = 0;
     MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
     MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
     float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // RGBA, linear-space multiplier
     float metallicFactor = 1.0f;                           // [0, 1]
     float roughnessFactor = 1.0f;                           // [0, 1]
   };
   ```
   No emissive factor, no normal-map reference, no occlusion/AO factor,
   no second texture slot — see this ADR's own Related Spec Non-Goals.
   Defaults (`baseColorFactor = (1,1,1,1)`, `metallicFactor =
   roughnessFactor = 1.0`) are chosen so that a Material asset authored
   under the pre-this-ADR grammar, re-cooked under the new schema with no
   new authored lines, decodes to values a `PbrDirectLit` shader would
   treat as "fully metallic, fully rough, white base-color multiplier" —
   an inert, safe default for a kind that does not (yet) exist for that
   asset, never consumed by `UnlitTextured`/`LitTextured`'s own
   realization path regardless of value.
2. **Authoring source grammar — three new, optional, fixed-prefix
   lines**, `atlantis_material_source_version: 1 → 2`. A version-2 source
   may add `base_color_factor: <r> <g> <b> <a>`, `metallic_factor: <v>`,
   `roughness_factor: <v>` lines; their absence in an otherwise-valid
   version-2 source is not an error — the parser applies the same
   defaults as item 1 above. A version-1 source is rejected outright
   (`SourceVersionError`-shaped, matching every existing Asset System
   grammar's "no dual-version reader" convention). Every currently
   checked-in `.material.txt` file (`unlit_textured_quad`,
   `lit_textured_quad`, and the CMake-test-only
   `cmake_material_declaration_test`) is re-authored only to the
   version-2 grammar's own version line — no existing file gains any of
   the three new lines, matching ADR-0060's own migration precedent
   exactly ("no existing node gains a `material=` token").
3. **Binary artifact — schema version bump, fixed new size, no dual
   reader.** The 32-byte header widens by exactly 24 bytes (`float4` = 16
   bytes + two `float`s = 8 bytes) to a new fixed size (Plan-time-named
   constant, e.g. `kMaterialArtifactHeaderSizeBytesV3 = 56`), encoded
   with the same little-endian, explicit-shift/mask discipline
   `material_artifact.cpp` already uses for every other field
   ([ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
   unconditional little-endian contract). Decode rejects any size other
   than the new fixed size outright — an old, 32-byte artifact is a
   real, distinct decode error (`UnexpectedSize`, unchanged enumerator,
   now triggered by the old size instead of a corrupted one), never
   silently accepted or defaulted.
4. **Metadata sidecar** widens to also carry `baseColorFactor`/
   `metallicFactor`/`roughnessFactor`, cross-validated against the
   artifact's own decoded values by `loadMaterialAsset()` exactly as
   `kind`/`textureAsset` already are today (`load_material.cpp:61-63`'s
   own pattern, widened by three more field comparisons) —
   `MetadataArtifactMismatch` remains the single, unchanged enumerator
   for any such disagreement.
5. **Cook-time value validation, both directions, matching this
   codebase's existing dual cook/decode validation discipline (Spec
   0020's own normal-length precedent) — never a naive parse-and-trust:**
   - `baseColorFactor`'s four components must each be finite and in
     `[0, 1]` — a color-managed reflectance factor outside this range is
     rejected at cook time with a new, distinct `CookError`-shaped
     enumerator (e.g. `BaseColorFactorOutOfRange`), never clamped
     silently. (A future spec may widen this to allow values above 1 for
     an artistic "over-bright" tint; this ADR does not anticipate that
     need without a real consumer.)
   - `metallicFactor`/`roughnessFactor` must each be finite and in
     `[0, 1]` — rejected at cook time with a new, distinct enumerator
     (e.g. `MaterialFactorOutOfRange`) on violation, including NaN/Inf,
     checked explicitly (matching Spec 0020's own explicit
     `std::isfinite` discipline for authored normals), never left to an
     unchecked `float` parse.
   - `loadMaterialAsset()` independently re-validates the same two range
     checks against the artifact's own decoded bytes (never trusting a
     well-formed cooker output, matching every existing Asset System
     loader's own re-validation discipline) — a corrupted or
     hand-edited artifact with an out-of-range or non-finite factor is a
     distinct `TextureLoadError`-shaped... **material**-load error
     enumerator (exact name a Plan-time detail, e.g.
     `MaterialFactorOutOfRange`), never silently clamped or accepted.
6. **Base-color texture color-space requirement — a new, real,
   explicit cross-validation, closing the exact gap ADR-0057's own
   Consequences already disclosed and left open.** A `PbrDirectLit`
   Material's own `textureAsset` must resolve to a texture whose
   metadata sidecar declares `colorSpace == TextureColorSpace::Srgb`.
   This check cannot run inside `cookMaterial()` itself (a material's
   own cook step, per ADR-0059 Decision 7, never resolves or opens its
   referenced texture's own artifact/metadata — value-level reference
   only, matching a scene's own dangling-mesh-reference precedent
   exactly) — it therefore runs at the same point, and by the same
   mechanism, Runtime already resolves a scene's material/texture
   dependency closure against the per-scene manifest
   ([ADR-0060](0060-scene-material-binding-and-runtime-transactional-resource-publish.md)
   Decision 6, Phase 1, CPU-only resolve/load, inside
   `loadAndInstantiateScene()`): once a `PbrDirectLit` material's own
   texture dependency is resolved and its metadata sidecar read (an
   already-necessary read — Runtime already reads a texture's metadata
   to load its `TextureAssetData`), Runtime additionally checks
   `colorSpace == Srgb` for that one case and fails the whole scene load
   with a new, distinct `RuntimeInitError` sub-code (e.g.
   `PbrBaseColorTextureNotSrgb`) on violation — the same severity and
   mechanism an unresolvable mesh/material `AssetId` reference already
   uses today (ADR-0060 Decision 6, Phase 1). `UnlitTextured`/
   `LitTextured` materials are entirely unaffected — this check runs
   only for a material whose own `kind == PbrDirectLit`. **This is
   Runtime-side validation on an existing, already-read value, not a
   new Asset System mechanism, a new artifact field, or a new decode
   path** — `TextureMetadata`'s own `colorSpace` field
   ([ADR-0057](0057-texture-asset-format-decoder-dependency-and-color-space-contract.md))
   is unmodified; this ADR only adds one new consumer of it.
7. **`baseColorFactor` is authored and consumed in linear space; the
   sampled base-color texture is authored and consumed as sRGB, decoded
   to linear by Vulkan's own fixed-function texture unit before the
   fragment shader ever sees a sampled value** — matching item 6's own
   requirement and ADR-0062's own already-confirmed fact that
   `Rgba8Srgb` sampling "already performs real, hardware sRGB→linear
   decode at sample time" (`adr/0062-...md:30-31`). The two are then
   combined by ordinary linear-space multiplication (this ADR's own
   Related Spec/Related ADR-0067 states the exact BRDF formula the
   product feeds into) — no additional gamma/color-space conversion is
   introduced anywhere in this chain.
8. **No new error enumerator where an existing one already fits**,
   matching ADR-0059's own Decision 8 discipline exactly: `BadMagic`,
   `UnsupportedSchemaVersion`, `SizeMismatch` (now triggered by the new
   fixed size), and `LogicalPathInvalid` are all reused unchanged; only
   the two genuinely new failure modes named in items 5 and 6 above
   (out-of-range/non-finite factor; base-color texture not sRGB) get
   their own new enumerators, settled precisely at Plan time.

## Consequences

### Positive

- Reuses the exact cook/artifact/metadata/load shape every other Asset
  System type already uses — zero new module, zero new mechanism, zero
  new third-party dependency.
- `UnlitTextured`/`LitTextured`'s own rendered pixels are provably
  unaffected: their own realization path (`selectShaderPair()`,
  `createMaterial()`) never reads the three new `MaterialAssetData`
  fields, and their own `.slang` source files are not modified by this
  ADR at all — only the CPU-side asset DTO widens, exactly matching
  ADR-0060's own "cooked artifacts change; rendered pixels do not"
  precedent for the Scene format's own version-2 migration.
- Closes a real, previously-disclosed-but-left-open gap (ADR-0057's own
  "no validation catches this" admission) using data that already exists
  (`TextureMetadata::colorSpace`) and an already-existing validation
  point (Runtime's own Phase 1 scene-dependency resolution) — no new
  Asset System machinery, no new manifest column, no new file.
- The dual cook+decode value-range validation for the three new fields
  follows an already-`Accepted`, already-shipped precedent (Spec 0020's
  normal-length discipline) exactly, rather than inventing a new
  validation shape.

### Negative / Trade-offs

- Every currently-checked-in Material asset (`unlit_textured_quad`,
  `lit_textured_quad`, and the CMake-test-only fixture) must be
  re-cooked under the new schema — a mechanical, disclosed,
  zero-rendered-pixel-impact migration, but a real one-time cost to
  every existing `.material.txt` file and its own cooked artifact bytes,
  matching the exact cost ADR-0060's own scene-format migration already
  accepted for an analogous reason.
- `PbrDirectLit`'s own base-color-texture-must-be-sRGB requirement is
  enforced only at Runtime scene-load time, not at Material cook time —
  a real, disclosed latency between authoring a wrong-color-space
  texture reference and discovering the mistake, inherent to ADR-0059's
  own existing "material cook never resolves its texture reference"
  boundary, which this ADR does not reopen.
- `baseColorFactor`/`metallicFactor`/`roughnessFactor` exist on every
  `MaterialAssetData` regardless of `kind`, including two kinds that
  never read them — a small, disclosed shape inefficiency (three unused
  floats per non-PBR Material), accepted in favor of a single,
  unconditional schema (no per-`MaterialKind` artifact sub-format),
  matching this codebase's own existing preference for one flat format
  per asset type over a tagged-union/variant artifact shape.
- The value-range validation (`[0, 1]` for all three parameters) is a
  real, disclosed restriction on artistic intent (no over-bright base
  color, no out-of-`[0,1]` metallic/roughness even for a deliberate
  stylized effect) — accepted for this foundation round, reversible
  later by a real, evidence-driven future ADR if a genuine consumer
  needs it.

## Alternatives Considered

- **A per-`MaterialKind` artifact sub-format** (a tagged union/variant
  binary layout, `PbrDirectLit` materials carrying the three new fields,
  other kinds not). Rejected: every other Asset System format in this
  codebase is one flat, fixed-size record per asset *type* (mesh, scene
  node, texture, material) — introducing per-`MaterialKind` structural
  variation inside the *material* artifact itself is a new, undesigned
  kind of format complexity (variable-size records, a discriminator-then-
  branch decode) this codebase has never needed and this ADR's own
  narrow three-scalar addition does not justify.
- **A separate, second Asset System type ("PbrMaterialAssetData")
  instead of widening `MaterialAssetData`.** Rejected: `MaterialKind`
  already exists as exactly the closed-enum "which shading behavior"
  discriminator this codebase uses (ADR-0059's own Decision 3); forking
  a second DTO/artifact/loader family for one more enumerator value
  duplicates the entire cook/artifact/metadata/load chain for no benefit
  ADR-0061's own `LitTextured` addition did not need.
- **Reject any base-color texture reference for `PbrDirectLit`
  entirely — scalar-only, no texture, `baseColorFactor` the sole color
  source.** Rejected: the Related Spec's own Non-Goals explicitly keep
  the single base-color texture (continuing the existing, one-texture-
  per-Material architecture this codebase already has); a texture-free
  PBR material would be a real, smaller, legitimate variant but is not
  what this ADR's own Related Spec was directed to evaluate first.
- **Infer/require `Rgba8Srgb` automatically for any texture referenced
  by a `PbrDirectLit` material, rather than validating and rejecting a
  mismatch.** Rejected: this codebase's own established convention
  (ADR-0057) is an explicit, author-supplied `colorSpace` cooker
  parameter, never inference from context — silently *forcing* a
  texture's own already-cooked color space based on which Material
  happens to reference it would be a new, undisclosed kind of
  cross-asset coupling (a texture's own cooked bytes/format now
  depending on which material references it, rather than being
  self-contained), and would also break instantly if two Materials of
  different kinds ever legitimately shared one texture `AssetId`.
- **Allow `baseColorFactor`/`metallicFactor`/`roughnessFactor` values
  outside `[0, 1]`, clamped at load or at shader time instead of
  rejected at cook/decode time.** Rejected for this round: this
  codebase's own established discipline (Spec 0020's normal-length
  check) rejects invalid authored data outright rather than silently
  correcting it — a silently-clamped out-of-range factor would produce
  a cooked artifact whose author never sees the correction, exactly the
  failure mode Spec 0020's own review explicitly closed for authored
  mesh normals.

## Amendment note

This ADR's own three new cook-time/decode-time error enumerators (items
5, 6, 8 above) and the exact new fixed artifact size (item 3) are
Plan-time details, named here by example only — finalized once, by the
eventual Plan, and re-confirmed against real Slang/C++ reflection rather
than self-certified, per this Spec's own Testing & Verification Plan.
