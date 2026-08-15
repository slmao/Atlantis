# shaders/minimal_renderer/

`minimal_mesh.slang` is the single Slang source module (checked in),
containing both `vertexMain`/`fragmentMain` entry points and sharing one
explicit varying-interface `struct` (ADR-0030's authoring convention).
Functionally equivalent to the retired GLSL pair (Spec 0007/ADR-0027):
camera uniform at `[[vk::binding(0, 0)]]`, a push-constant
object-to-world matrix, position/color vertex inputs at explicit
`[[vk::location(0)]]`/`[[vk::location(1)]]`, unlit per-vertex-color
output.

Per Spec 0008/Plan 0008, this module supersedes ADR-0027's temporary,
checked-in, pre-compiled SPIR-V bootstrap: `CMakeLists.txt` here calls
`atlantis_add_slang_shader_pair()` (defined in
`src/shader_system/CMakeLists.txt`), which builds `.slang` into a
build-tree `.spv`/reflection-JSON artifact pair via `atlantis_shader_compiler`
(Atlantis Tools) at build time -- `slangc -profile spirv_1_0`, mandatory
`spirv-val --target-env vulkan1.0`, and build-time descriptor-contract/
push-constant validation against this material's own fixed expectation.
No `.spv` or reflection JSON is checked into this directory or anywhere
else in the repository; both are generated, configuration-independent
build-tree artifacts (`${CMAKE_BINARY_DIR}/shaders/minimal_renderer/`),
regenerated automatically whenever `minimal_mesh.slang` changes.

Shared, as a single authoritative source, by both
`examples/minimal_renderer_demo` and
`tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` -- each consumer's
own CMake target depends on the `minimal_mesh_shaders` target and copies
the four build-tree artifacts (`.vert.spv`, `.vert.refl.json`,
`.frag.spv`, `.frag.refl.json`) next to its own build output via
`add_custom_command(... POST_BUILD ...)`; never duplicated as source.

## Editing this shader

Edit `minimal_mesh.slang` directly and rebuild -- no manual regeneration
step, no compiler-version bookkeeping to update by hand. CMake detects
the source change and re-invokes `atlantis_shader_compiler`
automatically; a build-time failure (a Slang compile error, a
descriptor-contract mismatch, a `spirv-val` failure) fails the build
with the real tool diagnostics, not a silently stale checked-in `.spv`.

See [specs/0008-shader-system-foundation.md](../../specs/0008-shader-system-foundation.md)
and [plans/0008-shader-system-foundation.md](../../plans/0008-shader-system-foundation.md)
for the full design.
