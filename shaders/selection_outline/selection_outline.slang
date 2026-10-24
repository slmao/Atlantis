// Plan 0066 P3 (ADR-0132 D3; Spec 0066 R4; J4): the selection outline's
// second pass -- a full-screen triangle that, at each pixel outside the mask,
// draws the outline colour when the mask covers a pixel within two pixels of
// it, and discards everywhere else. So the outline lies just outside each
// selected silhouette, over everything (no depth). The mask is sampled at
// texel centres, so the sampler's filter does not matter.

struct VertexInput {
    // The same oversized NDC triangle the output transform draws.
    [[vk::location(0)]] float2 clipPosition;
};

struct Varying {
    float4 position : SV_Position;
};

[[vk::binding(0, 0)]]
Sampler2D maskSampler;

// Mirrors renderer's SelectionOutlinePushConstants (viewport_overlay.cpp), 32 bytes.
struct PushConstants {
    float4 color;           // straight alpha, sRGB-authored
    float2 inverseExtent;   // 1 / the mask's (and target's) extent
    uint linearizeColor;    // 1 on an sRGB target
    uint padding;
};
[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

static const int kRadius = 2;

float3 srgbToLinear(float3 c) {
    float3 low = c / 12.92;
    float3 high = pow((c + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return float3(c.r <= 0.04045 ? low.r : high.r, c.g <= 0.04045 ? low.g : high.g, c.b <= 0.04045 ? low.b : high.b);
}

bool covered(float2 pixelCentre) {
    return maskSampler.SampleLevel(pixelCentre * pushConstants.inverseExtent, 0.0).r > 0.5;
}

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = float4(input.clipPosition, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    const float2 centre = input.position.xy;  // a pixel centre
    if (covered(centre)) discard;
    bool edge = false;
    for (int y = -kRadius; y <= kRadius; ++y) {
        for (int x = -kRadius; x <= kRadius; ++x) {
            if (x * x + y * y <= kRadius * kRadius && covered(centre + float2(x, y))) edge = true;
        }
    }
    if (!edge) discard;
    const float4 color = pushConstants.color;
    return pushConstants.linearizeColor != 0 ? float4(srgbToLinear(color.rgb), color.a) : color;
}
