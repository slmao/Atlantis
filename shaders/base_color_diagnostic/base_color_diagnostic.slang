struct CameraUniform {
    float4x4 view;
    float4x4 projection;
    float4 factor;
};

struct PushConstants {
    float4x4 objectToWorld;
};

struct VertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
};

struct Varying {
    float4 position : SV_Position;
    float2 uv;
    nointerpolation float3 factor;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;
[[vk::binding(1, 0)]] Sampler2D baseColorSampler;
[[vk::push_constant]] ConstantBuffer<PushConstants> pushConstants;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = mul(camera.projection, mul(camera.view, mul(pushConstants.objectToWorld, float4(input.position, 1.0))));
    output.uv = input.uv;
    output.factor = camera.factor.rgb;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    return float4(saturate(baseColorSampler.Sample(input.uv).rgb * input.factor), 1.0);
}
