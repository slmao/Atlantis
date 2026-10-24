// Plan 0066 P3 (ADR-0132 D3; Spec 0066 R3; J5): the Viewport overlay's
// primitives -- triangles, segments and points from
// renderer::ViewportOverlayDrawList, drawn over the finished frame from the
// frame's own camera. One vertex layout serves all three:
//   - a triangle vertex is its own position;
//   - a segment vertex carries both endpoints and which end and side of the
//     screen-space quad it is: the quad is expanded here from the endpoints'
//     clip positions and the width in pixels (no wide-lines feature);
//   - a point vertex is the point and one corner of a screen-space square.
// A screen-constant group's positions are offsets from its anchor: one unit
// is drawn as pixelsPerUnit pixels at the anchor's view depth, from this
// frame's camera, so a camera move needs no resend (Spec 0066 Q2 S-a).
// Colours are authored in sRGB with straight alpha; on an sRGB target they
// are converted to linear first, as the UI pass does (Plan 0056 J6).

struct VertexInput {
    [[vk::location(0)]] float3 position;  // triangle vertex, segment end A, or point
    [[vk::location(1)]] float3 other;     // segment end B
    [[vk::location(2)]] float4 color;     // straight alpha, sRGB-authored
    [[vk::location(3)]] float4 shape;     // x kind (0 triangle, 1 segment, 2 point); y, z corner; w pixels
    [[vk::location(4)]] float4 anchor;    // xyz anchor, w pixels per unit (0: a world vertex)
};

struct Varying {
    float4 position : SV_Position;
    float4 color;
};

struct Camera {
    float4x4 view;
    float4x4 projection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Camera> camera;

// Mirrors renderer's ViewportOverlayPushConstants (viewport_overlay.cpp), 16 bytes.
struct PushConstants {
    float2 viewportSize;   // the target's extent in pixels
    uint linearizeColor;   // 1 on an sRGB target
    uint padding;
};
[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

static const float kNearW = 1e-5;  // a clip w at or below this is behind the camera

float3 srgbToLinear(float3 c) {
    float3 low = c / 12.92;
    float3 high = pow((c + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return float3(c.r <= 0.04045 ? low.r : high.r, c.g <= 0.04045 ? low.g : high.g, c.b <= 0.04045 ? low.b : high.b);
}

float4 toClip(float3 world) {
    return mul(camera.projection, mul(camera.view, float4(world, 1.0)));
}

// A screen-constant offset scaled at the anchor's depth: a world length L at
// clip w spans L * projection[1][1] / w in NDC, i.e. half the viewport height
// per NDC unit -- so one pixel is 2 w / (projection[1][1] * height) world units
// (w is 1 under an orthographic projection).
float3 toWorld(float3 p, float4 anchor) {
    if (anchor.w <= 0.0) return p;
    const float w = max(toClip(anchor.xyz).w, kNearW);
    const float worldPerPixel = 2.0 * w / (abs(camera.projection[1][1]) * pushConstants.viewportSize.y);
    return anchor.xyz + p * (anchor.w * worldPerPixel);
}

// Pixels -> an NDC offset, scaled back to clip space by w.
float2 pixelOffset(float2 pixels, float w) {
    return pixels * 2.0 / pushConstants.viewportSize * w;
}

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.color = pushConstants.linearizeColor != 0 ? float4(srgbToLinear(input.color.rgb), input.color.a)
                                                     : input.color;
    const float halfPixels = input.shape.w * 0.5;
    if (input.shape.x < 0.5) {
        output.position = toClip(toWorld(input.position, input.anchor));
    } else if (input.shape.x < 1.5) {
        float4 a = toClip(toWorld(input.position, input.anchor));
        float4 b = toClip(toWorld(input.other, input.anchor));
        if (a.w <= kNearW && b.w <= kNearW) {
            output.position = float4(0.0, 0.0, -2.0, 1.0);  // wholly behind the camera: clipped away
            return output;
        }
        // Clip the segment to the near side of the camera before projecting it.
        if (a.w <= kNearW) a = lerp(a, b, (kNearW - a.w) / (b.w - a.w));
        if (b.w <= kNearW) b = lerp(b, a, (kNearW - b.w) / (a.w - b.w));
        const float2 alongPixels = (b.xy / b.w - a.xy / a.w) * pushConstants.viewportSize * 0.5;
        const float length = sqrt(dot(alongPixels, alongPixels));
        const float2 along = length > 1e-6 ? alongPixels / length : float2(1.0, 0.0);
        const float2 across = float2(-along.y, along.x);
        const bool atB = input.shape.y > 0.5;
        // Square caps: each end extends half the width along the segment.
        const float2 offset = across * input.shape.z * halfPixels + along * (atB ? halfPixels : -halfPixels);
        float4 end = atB ? b : a;
        end.xy += pixelOffset(offset, end.w);
        output.position = end;
    } else {
        float4 centre = toClip(toWorld(input.position, input.anchor));
        if (centre.w <= kNearW) {
            output.position = float4(0.0, 0.0, -2.0, 1.0);
            return output;
        }
        centre.xy += pixelOffset(float2(input.shape.y, input.shape.z) * halfPixels, centre.w);
        output.position = centre;
    }
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    return input.color;
}
