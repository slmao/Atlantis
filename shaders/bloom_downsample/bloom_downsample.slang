// Plan 0044 (Spec 0044 R2-R3, ADR-0092 Decision 2): one downsample step of
// the bloom chain -- D1 from hdr_color, and D2..D6 each from the level
// above. The same fullscreen triangle as the output transform; this
// pass's viewport is its own (smaller) target, so uv spans that target
// and the source is read through its own texel size.
//
// Filter: the 13-tap downsample of Jimenez 2014 (Filament's shape) --
// five overlapping 2x2 boxes, the centre box weighted 0.5 and the four
// corner boxes 0.125 each.
//
// D1 only (brightPass = 1): every tap first goes through the bright-pass
// -- max(c - threshold, 0), then highlight compression
// 1 / (1 + max3 / kBloomHighlight) (Filament's formula with the knee a
// parameter) -- and each box is then weighted 1 / (1 + max3(box)) and the
// result renormalised (the firefly weight, Plan 0044 P6 / ruling O3), so a
// single very hot texel cannot dominate the chain. Below the knee every
// tap is exactly 0, so D1 -- and the whole chain -- is exactly 0.

struct Varying {
    float4 position : SV_Position;
    float2 uv;
};

struct VertexInput {
    [[vk::location(0)]] float2 clipPosition;
};

[[vk::binding(0, 0)]]
Sampler2D sourceSampler;

// Mirrors renderer::BloomDownsamplePushConstants (bloom.h), 16 bytes.
struct BloomDownsamplePushConstants {
    float2 sourceTexelSize;
    float threshold;
    float brightPass;
};
[[vk::push_constant]]
ConstantBuffer<BloomDownsamplePushConstants> pushConstants;

// renderer::kBloomHighlight.
static const float kBloomHighlight = 1000.0;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = float4(input.clipPosition, 0.0, 1.0);
    output.uv = input.clipPosition * 0.5 + 0.5;
    return output;
}

float max3(float3 c) {
    return max(c.r, max(c.g, c.b));
}

float3 brightPass(float3 c) {
    float3 bright = max(c - float3(pushConstants.threshold, pushConstants.threshold, pushConstants.threshold),
                        float3(0.0, 0.0, 0.0));
    return bright * (1.0 / (1.0 + max3(bright) / kBloomHighlight));
}

float3 tap(float2 uv, float2 offsetTexels) {
    float3 c = sourceSampler.Sample(uv + offsetTexels * pushConstants.sourceTexelSize).rgb;
    return pushConstants.brightPass > 0.5 ? brightPass(c) : c;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float2 uv = input.uv;
    float3 a = tap(uv, float2(-2.0, 2.0));
    float3 b = tap(uv, float2(0.0, 2.0));
    float3 c = tap(uv, float2(2.0, 2.0));
    float3 d = tap(uv, float2(-1.0, 1.0));
    float3 e = tap(uv, float2(1.0, 1.0));
    float3 f = tap(uv, float2(-2.0, 0.0));
    float3 g = tap(uv, float2(0.0, 0.0));
    float3 h = tap(uv, float2(2.0, 0.0));
    float3 i = tap(uv, float2(-1.0, -1.0));
    float3 j = tap(uv, float2(1.0, -1.0));
    float3 k = tap(uv, float2(-2.0, -2.0));
    float3 l = tap(uv, float2(0.0, -2.0));
    float3 m = tap(uv, float2(2.0, -2.0));

    float3 centre = (d + e + i + j) * 0.25;
    float3 topLeft = (a + b + f + g) * 0.25;
    float3 topRight = (b + c + g + h) * 0.25;
    float3 bottomLeft = (f + g + k + l) * 0.25;
    float3 bottomRight = (g + h + l + m) * 0.25;

    if (pushConstants.brightPass > 0.5) {
        // The firefly weight: each box's contribution scaled by
        // 1 / (1 + max3(box)), then renormalised by the weights' sum.
        float wCentre = 0.5 / (1.0 + max3(centre));
        float wTopLeft = 0.125 / (1.0 + max3(topLeft));
        float wTopRight = 0.125 / (1.0 + max3(topRight));
        float wBottomLeft = 0.125 / (1.0 + max3(bottomLeft));
        float wBottomRight = 0.125 / (1.0 + max3(bottomRight));
        float3 weighted = centre * wCentre + topLeft * wTopLeft + topRight * wTopRight + bottomLeft * wBottomLeft +
                          bottomRight * wBottomRight;
        float weightSum = wCentre + wTopLeft + wTopRight + wBottomLeft + wBottomRight;
        return float4(weighted / weightSum, 1.0);
    }
    float3 result = centre * 0.5 + (topLeft + topRight + bottomLeft + bottomRight) * 0.125;
    return float4(result, 1.0);
}
