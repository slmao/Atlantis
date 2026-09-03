// Plan 0026 Milestone 4 (ADR-0071 P1-P3): the visible sky background,
// drawn once per frame, strictly before every scene DrawItem (ADR-0071's
// own Proposed Correction). Reuses the existing fixed fullscreen-triangle
// vertex schema unmodified (identical to output_transform_unorm.slang's
// own VertexInput below); its own per-pixel work happens entirely in
// fragmentMain, never in vertexMain, which is why binding 0 below
// reflects as Fragment-stage only (see skyExpectedDescriptorContract(),
// confirmed against a real slangc probe, Plan 0026's own P3).

struct Varying {
    float4 position : SV_Position;
    float2 uv;
};

struct CameraUniform {
    float4x4 view;
    float4x4 projection;
};

// Binding 0: the existing frame uniform. Referenced only by fragmentMain
// below -- vertexMain needs no camera data at all (P1's fixed clip depth
// is a literal, not derived from the camera).
[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera;

// Binding 1: the environment's own already-realized prefiltered cubemap
// (EnvironmentLighting::prefilteredEnvironment), contiguous at 1 since
// hasCameraUniformBinding == true (ADR-0070 P2's existing binding-index
// rule). Sampled at an explicit mip 0 -- P1's own un-blurred base level.
[[vk::binding(1, 0)]]
SamplerCube environmentSampler;

struct VertexInput {
    // The same fixed, oversized NDC triangle output_transform_unorm.slang's
    // own VertexInput already uses -- (-1,-1), (3,-1), (-1,3) -- reusing
    // the identical checked-in fullscreen-triangle vertex/index buffers
    // (Plan 0024 M5), never a new geometry asset.
    [[vk::location(0)]] float2 clipPosition;
};

// ADR-0071 P1: a fixed, named literal -- strictly less than the pass's
// own 1.0 depth clear, so the sky's own depth test (VK_COMPARE_OP_LESS,
// unchanged) passes wherever nothing has drawn yet. depthWriteEnabled ==
// false (the sky Pipeline's own PipelineCreateParams) means this value
// is never written back to the depth buffer.
static const float kSkyClipDepth = 0.999999;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = float4(input.clipPosition, kSkyClipDepth, 1.0);
    output.uv = input.clipPosition * 0.5 + 0.5;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    // ADR-0071 P2: reconstructs the per-pixel view ray from screen UV and
    // the existing projection matrix's own diagonal terms -- no separate
    // fovY/aspect uniform. camera.view's own upper-left 3x3 is orthonormal
    // (lookAtMatrix(), scene_extraction.cpp); its transpose is its inverse
    // rotation, so camera translation (view's own column-3 offset) is
    // never read -- the sky is invariant to camera translation by
    // construction, never a runtime branch.
    float2 ndc = input.uv * 2.0 - 1.0;
    float3 viewSpaceDir = normalize(float3(ndc.x / camera.projection[0][0], ndc.y / camera.projection[1][1], -1.0));
    float3x3 viewRotation = (float3x3)camera.view;
    float3 worldSpaceDir = normalize(mul(transpose(viewRotation), viewSpaceDir));

    // The identical world-space-direction-to-cubemap-sample convention
    // pbr_ibl.slang's own environment.SampleLevel(R, lod) call already
    // uses for specular reflection (Spec 0025) -- mip 0 is the directly
    // resampled, un-blurred base level (Plan 0025 P1).
    return environmentSampler.SampleLevel(worldSpaceDir, 0.0);
}
