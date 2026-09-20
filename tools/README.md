# tools/

Contents:

- [content/](content/) — pinned, SHA256-verifying fetch scripts for large external
  content that is never committed (Spec 0037 repository content policy), with the
  committed provenance and licence files for that content. `content/fetch_bistro.ps1`
  fetches the Bistro glTF scene into the gitignored `<repo>/content/bistro/`
  ([Plan 0037](../docs/plans/0037-gltf-importer.md) Milestone 2).

This is where Atlantis Tools (offline/dev tooling — asset processing,
shader precompilation, debug-capture glue) will live once specced. See
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-tools).
No runtime module depends on anything here.

Do not add tool sources here without a linked spec and plan. See
[AGENTS.md](../AGENTS.md).
