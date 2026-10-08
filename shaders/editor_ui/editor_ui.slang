// Plan 0056 P5 (ADR-0108 D3; J6): the UI overlay -- textured, vertex-coloured
// triangles from renderer::UiDrawList, positions in target pixels. Each draw
// samples one of two textures, chosen by a push constant: the UI font atlas
// (binding 0) or the Viewport, the scene rendered offscreen (binding 1).
//
// Colour spaces (J6): UI colours are authored in sRGB. On an sRGB target the
// hardware encodes on store, so the vertex colour is first converted to linear
// (linearizeVertexColor); on a UNORM target it is written as is. The Viewport
// is sampled through a view of its own format -- the swapchain's -- so its
// texels come back as the values that format stores and are written back
// unchanged; it is opaque (alpha forced to 1).

struct VertexInput {
    [[vk::location(0)]] float2 position;  // target pixels, origin top left
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float4 color;     // straight alpha, sRGB-authored
};

struct Varying {
    float4 position : SV_Position;
    float2 uv;
    float4 color;
};

[[vk::binding(0, 0)]]
Sampler2D fontAtlasSampler;

[[vk::binding(1, 0)]]
Sampler2D viewportSampler;

// Mirrors renderer::UiOverlayPushConstants (ui_overlay.cpp), 16 bytes.
struct UiPushConstants {
    float2 scale;                // 2 / display size: pixels -> clip space
    uint linearizeVertexColor;   // 1 on an sRGB target
    uint textureSlot;            // 0: font atlas, 1: Viewport
};
[[vk::push_constant]]
ConstantBuffer<UiPushConstants> pushConstants;

float3 srgbToLinear(float3 c) {
    float3 low = c / 12.92;
    float3 high = pow((c + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return float3(c.r <= 0.04045 ? low.r : high.r, c.g <= 0.04045 ? low.g : high.g, c.b <= 0.04045 ? low.b : high.b);
}

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = float4(input.position * pushConstants.scale - float2(1.0, 1.0), 0.0, 1.0);
    output.uv = input.uv;
    output.color = pushConstants.linearizeVertexColor != 0 ? float4(srgbToLinear(input.color.rgb), input.color.a)
                                                           : input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    if (pushConstants.textureSlot != 0) {
        float4 viewport = viewportSampler.Sample(input.uv);
        return float4(viewport.rgb, 1.0) * input.color;
    }
    return fontAtlasSampler.Sample(input.uv) * input.color;
}
