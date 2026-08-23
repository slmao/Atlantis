// Spec 0016 / Plan 0016 Milestone 9 (D11): the textured fixture's own
// shader -- a real combined image sampler binding
// ([[vk::binding(1, 0)]] Sampler2D, ADR-0056 Decision 9) alongside the
// existing camera-uniform/push-constant shape minimal_mesh.slang already
// establishes. No per-vertex color attribute -- the quads' own visible
// color comes entirely from the sampled texture, matching this Spec's
// own purpose (proving texture sampling, not vertex-color
// interpolation). The reflected shape of the Sampler2D binding
// (binding.kind == "descriptorTableSlot", type.kind == "resource",
// type.baseShape == "texture2D", type.combined == true) was confirmed
// via a real slangc probe compile of this exact shader during Plan
// 0016's own review and again during Milestone 9's own implementation,
// not assumed.

struct Varying {
    float4 position : SV_Position;
    float2 uv;
};

struct CameraUniform {
    float4x4 view;
    float4x4 projection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera;

struct PushConstants {
    float4x4 objectToWorld;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

struct VertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
};

[[vk::binding(1, 0)]]
Sampler2D texturedSampler;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position =
        mul(camera.projection, mul(camera.view, mul(pushConstants.objectToWorld, float4(input.position, 1.0))));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    return texturedSampler.Sample(input.uv);
}
