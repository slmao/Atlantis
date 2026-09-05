// Plan 0027 Milestone 4 (ADR-0072 D-3): the shadow-casting Pipeline's own
// shader -- position-only vertex input (the first genuinely single-
// attribute vertex schema in this codebase; every existing "minimal"
// layout reads position+color), one dedicated 128-byte light-space
// uniform (view+projection only, ADR-0072 D-6 -- a separate buffer from
// pbr_direct_lit.slang's/pbr_ibl.slang's own shared camera buffer,
// chosen to avoid coupling this minimal shader to their unrelated
// point-light/SH layout), and an ObjectToWorldOnly-shaped push constant.
// fragmentMain() is genuinely void -- no SV_Target output at all,
// matching hasColorAttachment = false (ADR-0072 D-2): a depth-only
// pipeline has no color attachment for an output to write into, and
// Vulkan itself does not require a fragment stage to produce one; this
// repository's own Device::createPipeline() is what currently requires
// a fragment module to exist at all (ADR-0072 D-3's own corrected
// claim), so this is the minimal-impact choice given that constraint.

struct VertexInput {
    [[vk::location(0)]] float3 position;
};

struct LightSpaceUniform {
    float4x4 view;
    float4x4 projection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<LightSpaceUniform> lightSpace;

struct PushConstants {
    float4x4 objectToWorld;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

[shader("vertex")]
float4 vertexMain(VertexInput input) : SV_Position {
    float4 worldPos = mul(pushConstants.objectToWorld, float4(input.position, 1.0));
    return mul(lightSpace.projection, mul(lightSpace.view, worldPos));
}

[shader("fragment")]
void fragmentMain() {}
