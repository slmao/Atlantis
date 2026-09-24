// Plan 0044 (Spec 0044 R2/R7, ADR-0092 Decisions 1-2): the bloom composite,
// at the full HDR extent, written to its own target, which the output
// transform then reads in place of hdr_color (ADR-0068 D-10 unchanged):
//   rgb = hdr + strength * upsample(U1) / kBloomLevelCount,  alpha = hdr's.
// Binding 0 is hdr_color, read at this pass's own pixel centres -- the
// same extent, so each texel comes back unfiltered and exact. Binding 1 is
// U1, read through the same 9-tap tent as bloom_upsample.slang. A bloom of
// exactly 0 leaves hdr bit for bit (Spec 0044 R7).

struct Varying {
    float4 position : SV_Position;
    float2 uv;
};

struct VertexInput {
    [[vk::location(0)]] float2 clipPosition;
};

[[vk::binding(0, 0)]]
Sampler2D hdrColorSampler;

[[vk::binding(1, 0)]]
Sampler2D bloomSampler;

// Mirrors renderer::BloomCompositePushConstants (bloom.h), 16 bytes.
struct BloomCompositePushConstants {
    float2 bloomTexelSize;
    float strength;
    float inverseLevelCount;
};
[[vk::push_constant]]
ConstantBuffer<BloomCompositePushConstants> pushConstants;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = float4(input.clipPosition, 0.0, 1.0);
    output.uv = input.clipPosition * 0.5 + 0.5;
    return output;
}

float3 tent(float2 uv, float2 texel) {
    float3 sum = bloomSampler.Sample(uv + float2(-texel.x, texel.y)).rgb +
                 bloomSampler.Sample(uv + float2(texel.x, texel.y)).rgb +
                 bloomSampler.Sample(uv + float2(-texel.x, -texel.y)).rgb +
                 bloomSampler.Sample(uv + float2(texel.x, -texel.y)).rgb;
    sum += 2.0 * (bloomSampler.Sample(uv + float2(0.0, texel.y)).rgb +
                  bloomSampler.Sample(uv + float2(0.0, -texel.y)).rgb +
                  bloomSampler.Sample(uv + float2(-texel.x, 0.0)).rgb +
                  bloomSampler.Sample(uv + float2(texel.x, 0.0)).rgb);
    sum += 4.0 * bloomSampler.Sample(uv).rgb;
    return sum * (1.0 / 16.0);
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float4 hdr = hdrColorSampler.Sample(input.uv);
    float3 bloom = tent(input.uv, pushConstants.bloomTexelSize);
    return float4(hdr.rgb + pushConstants.strength * bloom * pushConstants.inverseLevelCount, hdr.a);
}
