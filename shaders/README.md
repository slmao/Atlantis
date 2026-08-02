# shaders/

Empty by design.

Shader source layout, language (GLSL vs. HLSL vs. Slang), and the
build/compilation pipeline into artifacts consumed by the Vulkan Backend
are architectural decisions for the Shader System — see
[docs/render_graph/README.md](../docs/render_graph/README.md) and
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-shader-system)
for its place in the module map (currently `PROPOSED`, not approved).

Do not add shader files here without a linked spec and plan. See
[AGENTS.md](../AGENTS.md).
