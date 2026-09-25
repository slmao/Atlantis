# ADR 0094: Imported-Scene Assembly — One Build Step, a Cook-Manifest Mode, and an Authored Overlay

- **Status:** Accepted
- **Date:** 2026-09-25
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-09-25 (chat confirmation; reviewed in this
  branch's own PR, alongside Spec 0046's Approval)
- **Related Spec:** [Spec 0046: Bistro Finale](../specs/0046-bistro-finale.md) (`Approved`)
- **Related ADR(s):** builds on [ADR-0083](0083-gltf-to-atlantis-asset-format-mapping.md)
  and [ADR-0084](0084-gltf-importer-tools-subsystem-boundary.md) (the importer's
  outputs and its Tools boundary); extends the scene-asset declaration of
  Plan 0015 D7/D8 and Plan 0018 P9 (`atlantis_add_scene_asset()` and its
  dependency manifest) without changing it.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- The Runtime resolves a scene's mesh, material and texture references
  through a dependency manifest: one `logicalPath\tartifactPath\tmetadataPath`
  line per asset. `atlantis_add_scene_asset()` is its only producer, and it
  builds it from dependencies each declared by its own
  `atlantis_add_*_asset()` call, sources rooted at `assets/`.
- The glTF importer (ADR-0083/0084) writes one directory: 551 mesh
  **artifacts**, 254 material sources, one scene source, and a
  `cook_manifest.txt` listing one `atlantis_asset_cooker` invocation per
  texture, material and scene, with `{content_parent}`, `{import_dir}` and
  `{cooked_dir}` placeholders. For Bistro that is 1062 declared assets, and
  its sources are fetched content (`content/bistro/`, never committed,
  ~2.1 GB, SHA-256-pinned).
- Bistro has no lights and no camera; fog, bloom and exposure are camera
  tokens. Something must author them, reviewably and reproducibly.
- The importer rejects more than 1 directional or 4 point lights — Spec
  0019's cap, stale since Spec 0040 raised the grammar's and Runtime's to 64.

## Decision

1. **One build step per imported scene.** A CMake function (name fixed by
   the Plan; `atlantis_add_imported_scene` below) declares, for one glTF
   file, one custom command with one stamp that:
   1. runs `atlantis_gltf_importer` into a build-tree import directory,
      with the overlay (Decision 3);
   2. runs `atlantis_asset_cooker` in a new **cook-manifest mode**
      (Decision 2) over that import;
   3. exports the cooked scene's artifact, metadata and dependency-manifest
      paths as the same `ATLANTIS_<NAME>_*` variables
      `atlantis_add_scene_asset()` exports, so consumers are unchanged.

   Its inputs are the glTF, the overlay and the two tools; nothing else
   re-triggers it. The function is called only when the content is present
   at configure time; otherwise it declares nothing and says so in the
   configure log.
2. **A cook-manifest mode in `atlantis_asset_cooker`.** Given an import
   directory, a cooked-output directory and the content-parent directory,
   it executes every line of the import's `cook_manifest.txt` in-process
   (same code paths as the per-asset modes, same errors), then writes the
   dependency manifest: every asset in the import's `asset_list.txt` once —
   meshes pointing at their artifacts in the import directory, textures,
   materials and the scene at their cooked artifacts. Any line failing
   fails the step. The existing per-asset modes and
   `atlantis_add_*_asset()` are unchanged.
3. **An authored overlay, merged by the importer.** `--overlay=<file>`
   names a scene-v6 source holding only non-renderable nodes (the camera
   and lights). The importer appends its nodes after the imported ones,
   renumbering their ids above the imported maximum (parents inside the
   overlay are renumbered with them; an overlay node may not parent to an
   imported node), sets `active_camera` to the overlay's, and validates the
   merged scene against the grammar's limits. The importer's light cap
   becomes the grammar's (1 directional + 64 point).
4. **Scope.** The mechanism is general (any imported glTF), but this ADR
   admits exactly one use, Bistro; a second is a new decision.

## Consequences

### Positive

- A clean build reproduces the whole Bistro scene from the pinned content
  plus committed text: the overlay, the tools, the build files.
- The Runtime, the dependency-manifest format and every existing asset
  declaration are unchanged.
- Every hand-authored value (camera, exposure, fog, bloom, each light) is
  one line of a reviewable text file.
- The cooker, not a shell loop, runs ~500 cooks: one process, no
  per-line `std::system`, errors reported by the same code as today.

### Negative / Trade-offs

- All-or-nothing: changing one overlay light re-imports and re-cooks the
  whole set (~2 GB read). Acceptable at one scene; the Plan measures it.
- A new cooker mode and a new importer option are public tool CLI.
- Bistro's presence is configure-time state: a machine without the content
  builds and tests everything else, and has no `bistro` scene.

## Alternatives Considered

- **Generate 1062 `atlantis_add_*_asset()` calls at configure time** from
  the import's asset list. A thousand-target graph, configure-time import,
  and sources outside `assets/` that the existing functions do not accept.
  Rejected.
- **A cook script outside the build** (PowerShell), with paths handed to
  CMake. Not reproducible from a clean build, and the whitelist would point
  at state the build does not own. Rejected.
- **Commit the cooked Bistro set.** ~1.6 GB of derived content in git,
  against Spec 0037's content policy. Rejected.
- **Hand-edit the imported scene source.** Regenerated by every import;
  edits would be lost. Rejected.
- **Inject lights into the glTF.** Modifies pinned content and breaks its
  hashes. Rejected.
- **Hard-code Bistro's lights in the Runtime.** Scene-specific code, not
  reviewable as data. Rejected.
- **A second Runtime scene-composition feature** (load two scenes into one
  World). A Runtime capability for one use. Rejected.
