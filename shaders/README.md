# shaders/

**`minimal_renderer/`** — Spec 0007's fixed, temporary, checked-in vertex/
fragment SPIR-V pair (`minimal_mesh.{vert,frag}.glsl` source plus
pre-compiled `.spv` bytecode and a compiler/version note), per
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md):
no shader compiler, and no SPIR-V reflection, is invoked by any Atlantis
build target. This is a narrow, explicitly-bounded exception, not the
Shader System — see below.

Beyond `minimal_renderer/`, this directory remains empty by design.
General shader source layout, language (GLSL vs. HLSL vs. Slang), and a
real build/compilation pipeline into artifacts consumed by the Vulkan
Backend are architectural decisions for the future Shader System — see
[docs/render_graph/README.md](../docs/render_graph/README.md) and
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-shader-system)
for its place in the module map (currently `PROPOSED`, not approved; no
spec exists yet).

Do not add shader files here without a linked spec and plan. See
[AGENTS.md](../AGENTS.md).
