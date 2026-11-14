# ADR 0154: Managed Base Color Texture Import

- **Status:** Proposed
- **Date:** 2026-11-14
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0095](../specs/0095-texture-import-and-base-color-binding.md)

## Context

ADR-0147 establishes local managed registration and separate publication for
glTF. ADR-0153 makes a cooked texture GUID a dependency of the basic PBR
material recipe and uses the selected catalog for draft preview. A standalone
texture needs an authority for its source and color space and an identity
that an independently rebuilt material can resolve.

## Decision

1. Use the managed project as the authority for standalone PNGs. A registered
   texture owns one immutable PNG, its content seal, one stable GUID and
   one ordinary catalog fragment. The PNG subset is bounded by the existing
   preview path. Its sole color interpretation is sRGB and its cooked
   layout remains the existing one-mip RGBA8 form.
2. Extend the existing managed worker and generation metadata. The worker
   cooks the fixed private source snapshot, validates the single catalog
   addition and commits registration before publishing. The frozen producer
   evidence ties the PNG and cooked outputs together in a versioned view.
   It lets activation validate its own bytes and lets the worker distinguish
   an ahead registry from the active view. The registered seal lets Resume
   verify the sole newly registered source even when publication never
   created a complete generation. Use ADR-0147 and ADR-0153 recovery and
   explicit activation semantics.
3. Add a typed standalone texture import and status pair on the shared
   asset surface. Keep the glTF operation and its receipt shape intact.
   Texture status also reports repair independently of its terminal job.
   After activation expose the asset as an ordinary Texture. Material
   binding is the existing, separate GUID-based 0094 edit and preview.

## Consequences

- All clients share refusal, settlement and recovery semantics, while
  existing consumers of the catalog need no new discovery representation.
- Registered source PNGs, seals and fragments belong in project backups.
  Frozen PNGs increase generation storage. Fresh opted-in builds can
  reproduce the same texture and material identities.
- Per-texture verification adds bounded I/O to configuration, activation
  and asset jobs. The Windows executable uses its platform SHA-256 API.
  A pre-0095 executable needs a compatible earlier generation.
- Import and material edit each require a publication and explicit
  restart. Either can be settled and recovered independently.

## Alternatives Considered

- Infer slot and color space from filenames or PNG metadata. A fixed sRGB
  interpretation gives the first slot one reviewable meaning and avoids
  introducing a color management subsystem.
- Reuse the glTF root or change its request to dispatch on an added kind.
  Separate additive methods preserve the existing closed request and
  receipt meanings and reuse the same underlying worker.
- Upload raw bytes, bind from the inbox or combine import with material
  save. Those options would introduce a second file authority or couple
  two distinct commit and recovery outcomes.
