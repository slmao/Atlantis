// Spec 0099 test capture. Preserve one linear float through the RGBA8
// readback using the existing fullscreen output descriptor and ABI.
struct VertexInput { [[vk::location(0)]] float2 clipPosition; };
struct Varying { float4 position : SV_Position; float2 uv; };

[[vk::binding(0, 0)]] Sampler2D hdrColorSampler;
struct ExposurePushConstants { float exposureMultiplier; };
[[vk::push_constant]] ConstantBuffer<ExposurePushConstants> exposurePushConstants;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = float4(input.clipPosition, 0.0, 1.0);
    output.uv = input.clipPosition * 0.5 + 0.5;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    uint bits = asuint(hdrColorSampler.Sample(input.uv).r * exposurePushConstants.exposureMultiplier);
    return float4(bits & 255u, (bits >> 8u) & 255u, (bits >> 16u) & 255u, bits >> 24u) / 255.0;
}
