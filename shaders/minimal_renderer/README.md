# shaders/minimal_renderer/

Spec 0007 / Plan 0007 Section 12's shader bootstrap: `minimal_mesh.vert.glsl`/
`minimal_mesh.frag.glsl` are the human-readable GLSL source (checked in,
never built by any CMake target); `minimal_mesh.vert.spv`/
`minimal_mesh.frag.spv` are the pre-compiled SPIR-V bytecode checked in
alongside them, produced by a human/agent running `glslc` manually, per
ADR-0027 ("no shader compiler is invoked by any Atlantis build target").

Shared, as a single authoritative copy, by both
`examples/minimal_renderer_demo` and
`tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` -- each consumer's
own CMake target copies the `.spv` files next to its own build output via
`add_custom_command(... POST_BUILD ...)`; never duplicated as source.

## Compiler / version

```
shaderc v2026.3 v2026.3
spirv-tools v2026.3 v2022.4-1283-gb707790a
glslang 11.1.0-1493-g168d452a
Target: SPIR-V 1.0
```

(`glslc.exe --version`, from the Vulkan SDK installed at
`C:\VulkanSDK\1.4.357.0`.)

## Regeneration command

Run from this directory:

```
glslc.exe --target-env=vulkan1.0 -fshader-stage=vertex   -o minimal_mesh.vert.spv minimal_mesh.vert.glsl
glslc.exe --target-env=vulkan1.0 -fshader-stage=fragment -o minimal_mesh.frag.spv minimal_mesh.frag.glsl
```

`--target-env=vulkan1.0` matches the Vulkan Backend's own minimum
supported API version (unraised by this Plan -- ADR-0024's dual dynamic-
rendering path is capability-detected at runtime, not gated by a higher
`VkApplicationInfo::apiVersion` request). `-fshader-stage=...` is required
because these source files use a `.glsl` extension (not `.vert`/`.frag`),
which `glslc` cannot infer a stage from on its own.

Any future change to `minimal_mesh.{vert,frag}.glsl` must re-run this
exact command, updating the `.spv` files and this README's own recorded
compiler/version output together, in the same commit -- reviewable as an
ordinary binary-diff-plus-source-diff PR, the same way any other
checked-in test fixture is reviewed in this repository.
