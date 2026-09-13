# ADR 0081: PBR Material BRDF Extension — Clearcoat, Sheen, Anisotropy

- **Status:** Accepted
- **Date:** 2026-09-13
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-09-13 (chat confirmation; reviewed alongside
  this branch's own PR)
- **Related Spec:** [Spec 0035: Clearcoat, Sheen, and Anisotropy PBR Materials](../specs/0035-clearcoat-sheen-anisotropy-materials.md) (`Approved`)

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

Spec 0035 adds three new BRDF features — clearcoat, sheen, anisotropy —
to Atlantis's PBR material system. Two already-`Accepted` ADRs already
govern the relevant territory and must not be silently reinterpreted:

- **ADR-0066** (material asset parameter set / color-space contract):
  fixes `MaterialAssetData`'s current fields (`kind`, `textureAsset`,
  `filter`, `addressMode`, `baseColorFactor[4]`, `metallicFactor`,
  `roughnessFactor`, `normalMapTexture`), the 64-byte material-artifact
  header (schema version 3), and the linear-space convention for
  `baseColorFactor` and every scalar factor.
- **ADR-0067** (PBR direct-lighting BRDF / push-constant contract):
  fixes `PbrDirectLit`'s own real, Slang-and-MSVC-confirmed 96-byte
  push-constant layout (`src/renderer/src/pbr_push_constants.h`):
  `objectToWorld[16]` (64B) + `baseColorFactor[4]` (16B) +
  `metallicFactor` (4B) + `roughnessFactor` (4B) + 8B explicit padding =
  96 bytes, against Vulkan's own guaranteed-minimum 128-byte push-
  constant budget — **32 bytes of documented headroom**. ADR-0067's own
  Consequences already named this as thin: "A future spec needing a
  fourth per-object push-constant scalar has 32 bytes of headroom
  left." Its own Alternatives Considered rejected a per-material
  uniform buffer specifically because "96 of 128 already suffice" —
  reasoning that assumed no further widening was imminent.

`MaterialKind` (`src/asset_system/include/atlantis/asset_system/material_types.h`)
is a closed, 3-value enum: `UnlitTextured`, `LitTextured`, `PbrDirectLit`.
Real shader-pair *selection*, however, is not flat per-`MaterialKind` —
`selectShaderPair()` (`src/runtime/src/material_realization.cpp`) nests
two independent booleans, `hasNormalMap` and `environmentEnabled`,
inside the `PbrDirectLit` arm of its own switch, picking among 4 real,
already-compiled `.slang` pairs today (`pbr_direct_lit`,
`pbr_direct_lit_normal_map`, `pbr_ibl`, `pbr_ibl_normal_map`).

Estimating the three new BRDFs' own parameter cost against the
established Filament reference model (Spec 0035's own normative
citation):

| BRDF | New scalar parameters | Rough byte cost |
|---|---|---|
| Clearcoat | `clearcoatFactor`, `clearcoatRoughness` (+ optional clearcoat normal map, a texture/descriptor binding, zero push-constant cost) | ~8 bytes |
| Sheen | `sheenColor` (vec3), `sheenRoughness` | ~16-20 bytes once real vec3 alignment padding is accounted for |
| Anisotropy | `anisotropyFactor`, `anisotropyRotation` | ~8 bytes |

Summed naively (~32-36 bytes) against `PbrDirectLit`'s existing 32 bytes
of headroom, this is at or past the edge of Vulkan's 128-byte guarantee
— with **zero margin for real alignment padding**, which
`PbrPushConstants`'s own existing struct already needed 8 explicit bytes
of tail padding to satisfy even without any of these three additions.
Adding all three as combinable feature flags on the *same* `PbrDirectLit`
push-constant struct is not comfortably affordable within ADR-0067's own
established 128-byte-guarantee constraint, without either (a) widening
past the guaranteed minimum (device-dependent, a portability risk this
codebase's Vulkan-specific rules do not currently accept for a Phase 1
feature) or (b) moving to a uniform buffer (a real, separate
architectural change ADR-0067 explicitly declined to make).

Anisotropy's own vertex-data need was also investigated: Spec 0029/
ADR-0073 already added a mandatory per-vertex object-space tangent
(`xyz` + handedness `w`, `mesh_artifact.h`); Filament's own anisotropic
model rotates exactly this kind of existing tangent about the normal by
a scalar `anisotropyRotation` in the fragment stage — no new vertex
attribute, no mesh-artifact schema change.

## Decision

**Each of the three new BRDFs becomes its own new, mutually-exclusive
`MaterialKind` enumerator** — `PbrClearcoat`, `PbrSheen`,
`PbrAnisotropic` (exact identifiers: Plan-stage detail) — rather than
combinable feature flags added to the existing `PbrDirectLit` kind.

Each new kind gets:

1. **Its own material-asset schema fields**, added the same way
   `normalMapTexture` was added by ADR-0074/Spec 0029 (a new, optional
   field on `MaterialAssetData`, a material-artifact schema version
   bump) — not a modification of `PbrDirectLit`'s own existing fields.
2. **Its own independent push-constant struct**, starting from the same
   96-byte `objectToWorld`/`baseColorFactor`/`metallicFactor`/
   `roughnessFactor` base `PbrPushConstants` already establishes, plus
   only that kind's own new scalar fields — never all three kinds'
   fields combined into one struct. Each kind's own real byte layout
   must be confirmed via real Slang reflection plus an MSVC
   `static_assert` probe, exactly matching ADR-0067 D-3's own required
   method, at Plan/Implementation time — not fixed by this ADR.
3. **Its own dedicated pair of `.slang` shader pairs**, added as new arms
   to `selectShaderPair()`'s existing closed switch (and
   `sampledTextureBindingCountFor()`'s matching switch) — the same
   structural extension `PbrDirectLit`'s own `hasNormalMap`/
   `environmentEnabled` nesting already demonstrates is a workable
   pattern, just one level up (a new `MaterialKind` arm, itself
   `hasNormalMap`-nested exactly as `PbrDirectLit`'s own arm already is,
   rather than a new nested boolean inside the existing `PbrDirectLit`
   arm).
4. **Exactly two IBL-lit variants — `<kind>_ibl` and
   `<kind>_ibl_normal_map`** (Spec 0035 Requirement 4) — not "at
   minimum one, more to be decided": Spec 0035's own showcase scene is
   IBL-only, and every new kind supports the existing, optional
   `normalMapTexture` field (Spec 0029/ADR-0074) exactly as
   `PbrDirectLit` already does, so both variants are required, not
   optional coverage. A direct-lit variant of any new kind (a third,
   `_direct_lit`-style pair) is Spec 0035's own Out of Scope/Future
   Work, not decided here. This fixes each new kind's own variant count
   at exactly 2 — **6 new `.slang` pairs total** across the three new
   kinds, not an open-ended count.

`MaterialKind`'s existing three enumerators and `PbrDirectLit`'s own
existing 96-byte push-constant layout are **unchanged** by this decision
— this is a strictly additive extension of ADR-0066/ADR-0067, reusing
both ADRs' own established conventions (linear-space scalar factors,
texture-valued parameters as descriptor bindings rather than push-
constant fields, closed-switch `MaterialKind` dispatch) rather than
replacing them.

## Consequences

### Positive

- Each new BRDF's own push-constant layout stays comfortably inside
  Vulkan's 128-byte guaranteed minimum on its own — clearcoat and
  anisotropy both land around 104 bytes (96 + ~8), sheen around
  112-116 bytes even with alignment padding — with no need to widen
  past the portable guarantee or introduce a uniform buffer for this
  round.
- Matches Spec 0035's own "each BRDF independently verifiable" Goal
  directly and cheaply: one `MaterialKind`, one shader pair, one golden
  test, zero risk of an untested feature-flag *combination* (e.g.
  clearcoat+sheen together, which this decision never has to support or
  test, because it is architecturally impossible to select both at
  once).
- No shader-variant combinatorial explosion: three new kinds each add
  exactly 2 fixed variants of their own (IBL-lit, and IBL-lit +
  normal-mapped — Decision item 4; 6 new `.slang` pairs total) rather
  than multiplying against `PbrDirectLit`'s existing 4 variants with up
  to 3 new independent boolean axes (which would risk up to 32
  variants).
- Directly reuses every one of ADR-0066/ADR-0067's own established
  conventions — no new asset-schema philosophy, no new color-space
  rule, no new dispatch philosophy — genuinely an extension, not a
  parallel or competing system.

### Negative / Trade-offs

- **No material may combine two or more of {clearcoat, sheen,
  anisotropy}, or combine any of them with itself twice, on one
  surface.** A physically real combination (e.g. brushed metal under a
  clear lacquer coat) is not representable in Phase 1 under this
  decision. This is a real, disclosed scope limitation (Spec 0035's own
  Non-Goals), not silently foreclosed — a future ADR may introduce a
  fourth, combinable "extended" kind backed by a uniform buffer once a
  concrete need exists, superseding this one's "each kind is
  independent" framing for that specific new kind only.
  `MaterialKind`'s own enum keeps growing by one value per new
  mutually-exclusive BRDF under this decision — three new values now,
  more later if this pattern continues, which is a real, if modest,
  enum-growth cost future specs should weigh against the uniform-buffer
  alternative once composability actually matters.
- Six new `.slang` shader pairs (2 per new kind, Decision item 4) means
  six more compiled artifacts Android's own asset lock-step (Spec 0035's
  own Goals/Requirement 8) must track — a real, if mechanical,
  maintenance cost already accepted by every prior shader addition to
  this codebase.
- Per-kind push-constant byte budgets are tight enough (see Context's
  own table) that a future, even-modestly-larger fourth parameter added
  to any *one* of these three kinds could force that specific kind past
  128 bytes on its own, needing this same class of decision again for
  that one kind specifically — this ADR does not pre-solve that; it is
  flagged as a real, foreseeable follow-up pressure point.

## Alternatives Considered

- **Feature flags on the existing `PbrDirectLit` `MaterialKind`**
  (`clearcoatEnabled`/`sheenEnabled`/`anisotropyEnabled`, combinable,
  mirroring today's `hasNormalMap`/`environmentEnabled`): this is
  Filament's own actual approach (their real material system *does*
  support combining clearcoat+anisotropy+sheen on one material) and was
  seriously considered for that reason. Rejected for Phase 1 because:
  (1) the push-constant budget (Context above) cannot comfortably
  afford all three simultaneously without widening past Vulkan's
  128-byte guarantee or adopting a uniform buffer, neither of which
  this ADR wants to force through as a side effect of a materials
  spec; (2) shader-variant count would grow combinatorially (up to 32
  variants against today's 4) unless collapsed via specialization
  constants/`#define` permutation — a real shader-build-system capability
  this codebase does not yet have and this ADR does not want to add
  as a silent prerequisite; (3) Spec 0035's own Goal only needs each
  BRDF independently verifiable, which mutually-exclusive kinds satisfy
  more simply and with a smaller blast radius than combinable flags.
- **A per-material uniform buffer**, replacing or supplementing push
  constants for these three (and future) BRDFs, removing the 128-byte
  ceiling as a constraint entirely: the natural long-term answer once
  combinability is actually needed (see Consequences/Negative above) —
  rejected as *this* ADR's decision because it is a materially larger
  architectural change (a new descriptor binding, new buffer lifetime/
  update-frequency/pool-capacity decisions) than three independent,
  already-budget-fitting push-constant layouts require, and ADR-0067
  itself already declined this exact alternative for the same
  proportionality reason.
- **One combined `PbrExtended` `MaterialKind`** carrying all three
  features' parameters in one (larger, uniform-buffer-backed) struct,
  with per-instance flags selecting which are active: a middle ground
  between the two options above: rejected for the same "materially
  larger change than this Spec needs" reasoning as the uniform-buffer
  alternative — it still requires the uniform-buffer migration to avoid
  the same push-constant ceiling, just packaged as one kind instead of
  three.
- **Widening the push-constant budget past 128 bytes**, betting on this
  codebase's real target hardware (this session's own reference GPU,
  Intel Arc B370, and Android devices generally) supporting a larger
  guaranteed minimum than Vulkan's spec floor: rejected — ADR-0067
  already established designing against the guaranteed minimum, not
  observed hardware, as this codebase's own portability discipline; this
  ADR does not reopen that.

