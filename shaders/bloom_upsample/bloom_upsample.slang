// Plan 0044 (Spec 0044 R2, ADR-0092 Decision 2): one upsample step of the
// bloom chain, U(k) = D(k) + upsample(U(k+1)) for k = 5..1 (U6 is D6).
// It renders at D(k)'s extent: binding 0 is D(k), read at this pass's own
// pixel centres; binding 1 is the lower, half-size level, read through a
// 9-tap tent (1 2 1 / 2 4 2 / 1 2 1, /16) at the lower level's texel
// size. Zero in both inputs gives exactly zero.

struct Varying {
    float4 position : SV_Position;
    float2 uv;
};

struct VertexInput {
    [[vk::location(0)]] float2 clipPosition;
};

[[vk::binding(0, 0)]]
Sampler2D currentSampler;

[[vk::binding(1, 0)]]
Sampler2D lowerSampler;

// Mirrors renderer::BloomUpsamplePushConstants (bloom.h), 16 bytes.
struct BloomUpsamplePushConstants {
    float2 lowerTexelSize;
    float2 _pad;
};
[[vk::push_constant]]
ConstantBuffer<BloomUpsamplePushConstants> pushConstants;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = float4(input.clipPosition, 0.0, 1.0);
    output.uv = input.clipPosition * 0.5 + 0.5;
    return output;
}

float3 tent(float2 uv, float2 texel) {
    float3 sum = lowerSampler.Sample(uv + float2(-texel.x, texel.y)).rgb +
                 lowerSampler.Sample(uv + float2(texel.x, texel.y)).rgb +
                 lowerSampler.Sample(uv + float2(-texel.x, -texel.y)).rgb +
                 lowerSampler.Sample(uv + float2(texel.x, -texel.y)).rgb;
    sum += 2.0 * (lowerSampler.Sample(uv + float2(0.0, texel.y)).rgb +
                  lowerSampler.Sample(uv + float2(0.0, -texel.y)).rgb +
                  lowerSampler.Sample(uv + float2(-texel.x, 0.0)).rgb +
                  lowerSampler.Sample(uv + float2(texel.x, 0.0)).rgb);
    sum += 4.0 * lowerSampler.Sample(uv).rgb;
    return sum * (1.0 / 16.0);
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float3 current = currentSampler.Sample(input.uv).rgb;
    return float4(current + tent(input.uv, pushConstants.lowerTexelSize), 1.0);
}
