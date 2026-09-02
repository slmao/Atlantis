# Plan: Image-Based Lighting Foundation

- **Spec:** [specs/0025-image-based-lighting-foundation.md](../specs/0025-image-based-lighting-foundation.md) (`Approved`, including its 2026-09-03 Accepted Correction)
- **Status:** Approved / Ready for Implementation
- **Author:** slmao
- **Human Review Approval (2026-09-03):** Approved by the human maintainer's
  explicit confirmation in the Codex task after reviewing
  [PR #118](https://github.com/slmao/Atlantis/pull/118). This accepts P1–P4,
  all ten implementation gates below, and the complete verification checklist.
  Implementation is authorized only after this approval record merges to
  `main` through PR #118.

## Objective

Implement Spec 0025's first image-based-lighting slice end to end: deterministic
offline HDR environment cooking, CPU-only environment loading, the minimal RHI/
Vulkan cubemap+mip contract, an opt-in PBR IBL shader path, Runtime-owned GPU
realization, and one separately reviewed image-regression golden. Existing
no-environment rendering must remain byte-identical.

## Plan-Stage Decisions

These values close Spec 0025's remaining implementation choices and are
accepted by the Human Review approval recorded above.

### P1 — Environment artifact and preprocessing constants

- Authoring input is a Radiance `.hdr` equirectangular image decoded as four
  float channels by `stbi_loadf()` in Atlantis Tools; RGB is used, alpha is
  ignored. Width must be exactly twice height, both non-zero, each component
  finite and non-negative.
- The first checked-in source is a repository-authored, procedurally generated
  1024×512 studio environment with an adjacent provenance text file recording
  the generation command/tool version and SHA-256. It carries no third-party
  licensing dependency.
- Cooker output is fixed to a 256×256 cubemap, nine mips down to 1×1, and a
  128×128 DFG LUT. Cubemap layer order is `+X,-X,+Y,-Y,+Z,-Z`; each mip stores
  all six faces before the next mip, tightly packed and row-major.
- `.aenv` schema version 1 has a 60-byte little-endian header: 8-byte magic
  `ATLENV\0\0`; `u32 schemaVersion`; `u64 AssetId`; `u32 faceSize`,
  `mipCount`, `dfgWidth`, `dfgHeight`; then `u32 offset,size` pairs for SH,
  specular-cubemap, and DFG payloads. Offsets are contiguous in that order.
- SH payload is nine padded RGB `float4` values (144 bytes), serialized as
  IEEE-754 binary32 little-endian. Specular payload is RGBA binary16; DFG is RG
  binary16. The decoder rejects invalid magic/version, zero or non-power-of-two
  dimensions, a mip count other than `log2(faceSize)+1`, overflow, overlapping
  or non-contiguous ranges, total-size mismatch, non-finite half/float values,
  or metadata/embedded/path-derived `AssetId` disagreement.
- The 2026-09-03 Accepted Correction governs identity: the logical path is
  normalized and hashed by existing `computeAssetId()` (ADR-0044). Cooker
  determinism and asset identity remain separate tests.
- Projection is single-threaded and fixed-order. Equirectangular lookup uses
  `u = atan2(z,x)/(2*pi)+0.5`, `v = acos(clamp(y,-1,1))/pi`, bilinear filtering,
  horizontal wrap, and vertical clamp. Cubemap face orientation is pinned in
  code and unit-tested by axis-colored input.
- SH uses the real, orthonormal, `Y`-up, band-0-through-2 basis in this order:
  `1`, `z`, `y`, `x`, `xz`, `zy`, `3y²-1`, `xy`, `x²-z²`, with the standard
  normalization constants `0.282095`, `0.488603`, `1.092548`, `0.315392`, and
  `0.546274`. Projection uses source-texel solid-angle weights and double
  accumulators; cosine convolution applies band factors `pi`, `2*pi/3`, and
  `pi/4` before conversion to float32.
- Specular mip 0 is the directly resampled environment. Mips 1–8 use a fixed
  1024-sample Hammersley sequence and GGX importance sampling, with perceptual
  roughness `mip/(mipCount-1)` and `alpha=max(roughness²,1e-3)`. The DFG LUT
  uses the same GGX convention and 1024 deterministic samples per texel.

### P2 — Closed RHI/Vulkan sampled-image widening

- Add `SampledTextureDimension::{Texture2D,TextureCube}`;
  `SampledTextureFormat::{Rgba16Float,Rg16Float}`; and
  `MipFilter::{Nearest,Linear}`. Existing enum values remain unchanged.
- Extend `SampledTextureCreateParams` with `dimension=Texture2D` and
  `mipLevelCount=1`; extend `SamplerCreateParams` with
  `mipFilter=Nearest`, `minLod=0`, and `maxLod=0`. Existing designated/default
  construction preserves current behavior.
- Add `SampledTextureUploadRegion { bufferOffsetBytes, mipLevel, arrayLayer,
  extent }` and a `copyBufferToTexture()` overload accepting
  `std::span<const SampledTextureUploadRegion>`. Keep the existing full-2D
  overload as the single-region compatibility path.
- Replace `PipelineCreateParams::hasSampledTextureBinding` with
  `sampledTextureBindingCount` (`0`, `1`, or `3` in this Plan). Replace the
  implicit `bindTexture(texture,sampler)` contract with
  `bindTexture(binding,texture,sampler)` and update every call site explicitly.
  Bindings are contiguous from `0` when `hasCameraUniformBinding=false`, or
  from `1` when it is true.
- Invalid dimensions, non-square cubes, out-of-range mip/layer/region values,
  and binding indices outside the bound Pipeline's declared range are
  programmer errors (`ATLANTIS_CHECK`). Missing device format/image-cube
  capabilities remain recoverable `SampledTextureCreateError` results.
- Vulkan creates cube images with six array layers and
  `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, cube views with
  `VK_IMAGE_VIEW_TYPE_CUBE`, and barriers spanning every mip/layer. Upload
  regions map one-for-one to `VkBufferImageCopy`; image state remains tracked
  for the whole resource, never per subresource.
- Every descriptor pool keeps the existing 4/8/16/32 set generations but
  allocates `3 * maxSets` combined-image-sampler descriptors. The 60-live-set
  ceiling is unchanged; only per-set sampler budget changes. A Pipeline still
  owns exactly one descriptor set.

### P3 — No-environment compatibility and IBL binding

- Keep `shaders/pbr_direct_lit/pbr_direct_lit.slang` byte-for-byte unchanged.
  Add a separate `pbr_ibl.slang` pipeline variant used only when BootstrapConfig
  names a valid environment. This structurally preserves every existing
  no-environment golden and avoids statically-used but unbound descriptors.
- Add `MaterialEnvironmentBinding::{None,Ibl}` as an immutable Material
  discriminator. Existing construction defaults to `None`; environment-enabled
  PBR materials use `Ibl`. No new `MaterialKind` or asset-schema field exists.
- Add a Renderer `EnvironmentLighting` borrowed view containing the prefiltered
  cubemap/LUT textures and samplers. Append
  `const EnvironmentLighting* environmentLighting = nullptr` to
  `Renderer::drawFrame()`. An `Ibl` Material with a null view is a programmer
  error; `None` never reads or binds the view.
- Binding 0 remains the frame uniform; binding 1 remains base color; binding 2
  is the prefiltered cube; binding 3 is the DFG LUT. The output-transform
  pipeline remains one sampler at binding 0 with no frame uniform.
- Extend the frame buffer from 320 to 464 bytes by appending nine padded
  `float4` SH coefficients at offset 320. No-environment callers write 144 zero
  bytes; existing shaders only declare/read their current prefix.

### P4 — Shader math contract

`pbr_ibl.slang` retains Spec 0023's direct-light loops unchanged and adds this
scene-linear term before returning to Spec 0024's HDR target:

```
NdotV = max(dot(N, V), 1e-4)
F0 = lerp(float3(0.04), baseColor, metallic)
Fibl = F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0)
kD = (1.0 - Fibl) * (1.0 - metallic)
irradiance = max(evaluateIrradianceSH(N), 0.0)
diffuseIbl = kD * baseColor * irradiance / pi
R = reflect(-V, N)
lod = roughness * (specularMipCount - 1)
prefiltered = environment.SampleLevel(R, lod).rgb
dfg = dfgLut.Sample(float2(NdotV, roughness)).rg
specularIbl = prefiltered * (F0 * dfg.x + dfg.y)
accumulated += diffuseIbl + specularIbl
```

The cooker and CPU reference tests use the same `roughness²` GGX alpha floor,
coordinate convention, and split-sum definition. No intensity, rotation,
multi-scattering compensation, occlusion, or skybox term is added.

## Milestones / Task Breakdown

### Milestone 1 — Environment CPU data, artifact, metadata, and math core

- Add `environment_types.h`, `environment_artifact.h/.cpp`,
  `environment_metadata.h/.cpp`, `cook_environment.h/.cpp`, and
  `load_environment.h/.cpp` under Asset System.
- Add environment-specific cook/decode/load error enums and exhaustive message
  mapping. Use explicit little-endian byte assembly; no struct `memcpy`.
- Implement deterministic equirectangular sampling, cube mapping, SH,
  Hammersley/GGX prefiltering, DFG integration, and binary16 conversion in
  private Asset System source helpers.
- Tests land with this milestone: exact header/payload bytes, round-trip,
  malformed offsets/sizes/mips/non-finite values, axis orientation, SH constant
  environment, GGX/DFG reference points, same-path determinism, and different-
  path ID distinction.

### Milestone 2 — Tools and build-time asset integration

- Add `AssetKind::Environment`, `--kind=environment`, `.hdr` validation, and
  `stbi_loadf()` decode in the existing asset-cooker target; update its “one
  stb implementation TU” comment rather than adding another implementation TU.
- Add `atlantis_add_environment_asset(NAME SOURCE)` mirroring texture asset
  stamp/artifact/metadata exports and declared-logical-path validation.
- Add the procedural studio `.hdr` source/provenance and CMake declaration;
  prove a clean build cooks it to `.aenv`/metadata and a second identical cook
  is byte-for-byte identical.

### Milestone 3 — RHI type and command-surface widening

- Implement P2's enums/create parameters/accessors/upload region, compatibility
  overload, explicit binding index, and `sampledTextureBindingCount` migration.
- Update RHI mocks and all compile-time/call-site tests in one atomic commit so
  no intermediate revision has a mismatched virtual interface.
- Add GPU-independent tests for default compatibility, cube/mip preconditions,
  region validation, binding range checks, and equality operators.

### Milestone 4 — Vulkan sampled images, uploads, samplers, and pool capacity

- Extend format mapping/capability checks for `R16G16B16A16_SFLOAT` and
  `R16G16_SFLOAT`; implement cube allocation/view, full-subresource barriers,
  region copies, mip sampler state, and explicit descriptor binding writes.
- Rework descriptor layout construction as the closed contiguous 0/1/3 count;
  resize combined-sampler pool entries to `3*maxSets` without changing the
  generation/set-capacity algorithm.
- GPU tests upload face/mip sentinels and verify 2D/cube/LUT sampling, all-mip
  transitions, capability-error mapping, repeated pool growth/reuse, and clean
  destruction with Validation Layers enabled.

### Milestone 5 — Shader reflection contract and `pbr_ibl`

- Add `pbrIblExpectedDescriptorContract()` with binding 0 uniform visible to
  vertex+fragment and bindings 1–3 combined samplers visible to fragment.
- Extend shader compiler selection/validation and CMake shader targets. Keep
  the existing PBR shader target untouched; add `pbr_ibl` as a sibling.
- Implement P3/P4's 464-byte uniform and exact indirect math. Reflection tests
  verify descriptor slots, stage visibility, SH offset/size, push-constant
  layout, and mutation failures.

### Milestone 6 — Renderer frame-scoped environment binding

- Add `environment_lighting.h`, `MaterialEnvironmentBinding`, immutable
  Material storage/accessor, and the trailing nullable `drawFrame()` argument.
- Bind base color explicitly at 1 for textured materials, cube at 2 and DFG at
  3 only for `Ibl`, and output transform explicitly at 0.
- Renderer tests cover null/no-IBL compatibility, IBL invariant failure,
  explicit binding order, one bind per expected slot, and stateless borrowing.

### Milestone 7 — Runtime loading, realization, and lifecycle

- Extend `BootstrapConfig` with optional environment artifact/metadata and
  conditional `pbrIbl` shader paths. Both environment paths must be empty or
  both non-empty; IBL shader paths are required only when an environment exists.
- Load CPU `EnvironmentAssetData` in initialization without publishing GPU
  state. Extend the camera buffer/SH write and add Runtime-private
  `EnvironmentLightingResources`/candidate realization.
- On the first drawable frame, create cube/LUT/samplers/staging buffers, record
  both uploads before material uploads/draw in the same CommandList, and use the
  local candidate for that frame. After successful submit+`waitIdle()`, publish
  persistent resources and release staging/CPU payload; on any failure, RAII
  discards the unpublished candidate and the frame fails through explicit
  existing/new `RuntimeInitError`/lifecycle classifications.
- Material realization selects unchanged `pbr_direct_lit` when no environment
  exists and `pbr_ibl` plus `MaterialEnvironmentBinding::Ibl` otherwise. The
  environment is fixed for the RuntimeApplication lifetime; no rebuild path.
- Destruction order places environment resources after borrowing Materials but
  before Device destruction, matching existing reverse-declaration RAII rules.

### Milestone 8 — Runtime/tool/RHI integration tests

- Add CPU tests for incomplete config, environment load/decode failures,
  conditional shader validation, 464-byte SH publication, candidate publish/
  retry behavior, and no-environment selection of the old PBR shader.
- Add real-GPU tests for one-command-list upload+draw, diffuse-only IBL with
  zero direct lights, metallic reflection, roughness-to-mip response, direct+
  indirect additivity, repeated frames, shutdown, and Debug/Release Validation
  Layer cleanliness.
- Re-run the Spec 0021 descriptor-pool stress test with three-sampler PBR sets;
  prove set capacity remains 60 and sampler descriptors cannot exhaust first.

### Milestone 9 — New IBL demo fixture, without golden

- Add `ibl_material_demo.scene.txt` by reusing the four PBR spheres/materials
  and camera but declaring no Light nodes. Add a new image-regression fixture,
  golden generator, and GPU tests using the studio environment.
- Before a golden exists, prove four-sphere coverage, non-black dielectric
  diffuse, colored metallic response, smooth/rough distinction, deterministic
  repeated output, and exactly one environment upload/shared binding.
- Confirm every existing no-environment golden PNG and sidecar is byte-for-byte
  unchanged; none is regenerated.

### Milestone 10 — Golden capture and final verification

- On a clean implementation commit, capture only
  `ibl_material_demo/ibl_material_demo_512x512_rgba8unorm.{png,sidecar.txt}` in
  a separate commit. Record GPU/driver, source `.hdr` SHA-256, `.aenv` SHA-256,
  cooker schema/settings, and golden-update reason.
- Human-review the image for the four required visual distinctions before the
  golden commit is accepted. Add the capture-compare test only with that commit.
- Run the complete Verification Checklist below and attach counts/log evidence
  to the implementation PR.

## Files / Modules Touched (expected)

- Asset System: `src/asset_system/include/atlantis/asset_system/{errors.h,
  environment_types.h,environment_artifact.h,environment_metadata.h,
  cook_environment.h,load_environment.h}`, matching `.cpp` files under
  `src/asset_system/src/`, and `src/asset_system/CMakeLists.txt`.
- Tools/assets: `src/tools/asset_cooker/{cook_command.h,cook_command.cpp,
  main.cpp,CMakeLists.txt}`, `assets/CMakeLists.txt`,
  `assets/environments/ibl_studio_source.{hdr,provenance.txt}`, and
  `assets/scenes/ibl_material_demo.scene.txt`.
- RHI/Vulkan: `src/rhi/include/atlantis/rhi/{types.h,sampled_texture.h,
  sampler.h,command_list.h}`, `src/rhi/src/types.cpp`,
  `src/vulkan_backend/src/{vulkan_device.h,vulkan_device.cpp,
  vulkan_sampled_texture.h,vulkan_sampled_texture.cpp,vulkan_sampler.h,
  vulkan_sampler.cpp,vulkan_command_list.h,vulkan_command_list.cpp,
  vulkan_result.h,vulkan_result.cpp}`.
- Shader/Renderer: `src/shader_system/include/atlantis/shader_system/
  descriptor_contract.h`, `src/shader_system/src/descriptor_contract.cpp`,
  `src/tools/shader_compiler/{compile_and_validate.cpp,main.cpp}`,
  `shaders/pbr_ibl/**` (new), `src/renderer/include/atlantis/renderer/
  {environment_lighting.h,material.h,renderer.h}`,
  `src/renderer/src/{material.cpp,renderer.cpp}`.
- Runtime: `src/runtime/include/atlantis/runtime/{bootstrap_config.h,
  init_error.h,runtime_application.h,material_realization.h,
  environment_realization.h}`, `src/runtime/src/{runtime_application.cpp,
  material_realization.cpp,environment_realization.cpp}`, Runtime executable/
  test CMake files that populate `BootstrapConfig`.
- Tests: corresponding files under `tests/{asset_system,tools/asset_cooker,
  rhi,vulkan_backend,shader_system,renderer,runtime,image_regression}`;
  `tests/image_regression/fixture/ibl_material_demo_fixture.{h,cpp}`,
  `tests/image_regression/ibl_material_demo_gpu_tests.cpp`, generator/CMake,
  and the one new golden directory.
- Documentation/status: this Plan, Spec 0025's related-plan/Accepted-Correction
  record, ADR-0069's Accepted Amendment record, and registry/status summaries.

Any implementation-touched source outside this list is a disclosed Plan
deviation. Generated build outputs are never committed.

## Sequencing & Dependencies

M1 → M2 establishes the CPU artifact before any consumer. M3 → M4 lands the
backend-neutral contract before Vulkan. M1–M4 unblock M5; M5/M6 can then land
in either order but both are required by M7. M7 requires the CPU loader, RHI,
Vulkan, shader, and Renderer contracts. M8 follows M7. M9 follows all production
code and tests; M10 follows a clean M9 implementation commit and remains a
separate human-reviewed golden commit.

Atomic commits must keep each virtual RHI interface synchronized with every
implementation/mock, each artifact schema synchronized with decoder/tests, and
the `pbr_ibl` descriptor contract synchronized with shader reflection. No
partial commit may make an existing target uncompilable.

## Verification Checklist

1. [ ] Asset CPU tests: schema/header exact bytes; round-trip; every corruption
   class; finite/non-negative source; path-derived identity; deterministic SH,
   cubemap, DFG, and binary16 output; metadata cross-check.
2. [ ] Tool tests: real `.hdr` decode, CLI argument/error mapping, two-process
   deterministic cook, CMake stamp/byproduct behavior, asset-set validation.
3. [ ] RHI/Vulkan tests: default 2D compatibility; cube/mip/LUT creation;
   subresource upload; explicit binding slots; all-layer/mip transitions;
   sampler LOD; capability errors; descriptor-pool 3× sizing/growth/reuse.
4. [ ] Shader/Renderer tests: exact reflection/layout; P4 CPU reference values;
   old PBR shader unchanged; nullable frame input; IBL binding/invariant/order;
   no retained resource or platform/Vulkan dependency.
5. [ ] Runtime tests: config/load/realization failures; zero/no-environment SH;
   local-candidate same-frame use; publish only after wait; retry and shutdown;
   old-vs-IBL pipeline selection.
6. [ ] Real-GPU behavior: diffuse/specular/roughness/direct-additive cases,
   repeated frames, windowed and offscreen origins, both final target format
   classes, Debug and Release, Vulkan Validation Layers with zero warnings/errors.
7. [ ] Image regression: all seven existing goldens and sidecars byte-identical;
   new IBL fixture first, then one separately committed/captured/human-reviewed
   golden and capture-compare case under ADR-0042.
8. [ ] Full `ctest -LE gpu` and `ctest -L gpu`, Debug and Release; exact test
   counts recorded in the implementation PR.
9. [ ] Fresh `ATLANTIS_BUILD_TESTS=OFF` configure/build cooks the environment
   and produces a working `atlantis_runtime.exe` with zero test executables.
10. [ ] `/w14062 /WX` negative probes cover new `AssetKind`, sampled format/
    dimension/mip-filter, Material binding, and error-classification switches.
11. [ ] Module/link/include scan: Asset System links Core only; Renderer links
    Core/RHI/RenderGraph only; only Vulkan Backend names `Vk*`; no World/Scene
    schema change; no new third-party dependency.
12. [ ] `git diff --check` and committed-file audit clean; no generated `.spv`,
    reflection JSON, cooker output, failure PNG, or build directory committed.

## Rollback Plan

Revert M10's golden/capture test first, then M9's fixture. Revert M8 through M1
in reverse dependency order. RHI interface and every implementation/mock are an
atomic rollback group; artifact schema/cooker/decoder/tests are another atomic
group. The old `pbr_direct_lit` shader and no-environment path remain present
throughout, so a full rollback restores the pre-Spec-0025 behavior without
golden regeneration or asset migration. Never retain a cooked `.aenv` produced
by a reverted schema version.

## Decisions for Human Review

Approval of this Plan accepts P1–P4 and the following implementation gates:

1. The fixed 256/9-mip cubemap, 128 DFG, 1024-sample offline quality baseline.
2. The exact 60-byte `.aenv` schema and path-derived ADR-0044 identity.
3. A separate `pbr_ibl` shader/pipeline variant so the old path stays exact.
4. The closed contiguous 0/1/3 sampled-binding-count RHI contract, not a generic
   descriptor-layout system.
5. Whole-image, not per-subresource, state tracking.
6. Descriptor-pool sampler capacity `3*maxSets`, with the 60-set ceiling intact.
7. The 464-byte frame-uniform layout and exact P4 equations.
8. Runtime bootstrap selection fixed for one application lifetime; no World/
   Scene environment field or live environment swap.
9. First-frame upload and draw share one CommandList; publish waits for GPU
   completion and releases staging/CPU payload afterward.
10. One new fixture/golden only; every existing golden remains byte-identical.

The approval recorded at the top of this Plan is the distinct Spec+Plan
implementation gate required by AGENTS.md. Implementation remains blocked
until this approval record merges to `main` through PR #118.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No deltas beyond the Verification Checklist and explicit human-reviewed golden
discipline above.
