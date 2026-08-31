// Plan 0024 Milestone 4 (ADR-0068 D-3/D-5/D-6/D-10): the output-
// transform pass's own *_Srgb variant -- reads the scene-referred
// linear HDR intermediate (HdrColorTarget), applies the fixed-exposure
// Reinhard tone-mapping curve (ADR-0068 D-5), and writes the LINEAR
// tonemapped value unencoded -- the target image's own VK_FORMAT_*_SRGB
// view causes the GPU's fixed-function output-merger to perform the
// identical sRGB encode in hardware on store. This shader never applies
// the manual OETF -- doing so would double-encode (ADR-0068 D-6's own
// double-encode-avoidance rule); see output_transform_unorm.slang for
// the sibling variant that does apply it, for a *_Unorm final target.
// Never branched by MaterialKind -- all three existing MaterialKinds'
// own accumulated linear radiance passes through this one shared pass
// identically. Drawn as one fullscreen triangle via the existing
// drawIndexed() path -- no non-indexed draw API. Shares its own
// descriptor contract (one Sampler binding, no uniform buffer, no push
// constant) with output_transform_unorm.slang's own *_Unorm variant --
// see outputTransformExpectedDescriptorContract(). Identical vertex
// stage and identical tone-mapping math up to `tonemapped` as that
// sibling file -- the one and only difference is this fragment stage's
// own final return statement.

struct Varying {
    float4 position : SV_Position;
    float2 uv;
};

struct VertexInput {
    // A fixed, oversized NDC triangle -- (-1,-1), (3,-1), (-1,3) --
    // covering the full screen with exactly 3 vertices, baked into a
    // tiny, checked-in, persistent vertex buffer (never scene content,
    // never an Asset-System asset). No UV/normal/color attribute --
    // this pass's own UV is derived from clipPosition below.
    [[vk::location(0)]] float2 clipPosition;
};

// Binding 0, not 1 -- this pass's own sole binding (no uniform buffer
// at binding 0, unlike every MaterialKind's own contract), matching
// outputTransformExpectedDescriptorContract() exactly.
[[vk::binding(0, 0)]]
Sampler2D hdrColorSampler;

// ADR-0068 D-5: fixed, named constant -- no auto-exposure.
static const float kBaselineExposure = 1.0;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = float4(input.clipPosition, 0.0, 1.0);
    output.uv = input.clipPosition * 0.5 + 0.5;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float4 hdrColor = hdrColorSampler.Sample(input.uv);

    // ADR-0068 D-5: the max(...,0) floor is the *only* new clamp this
    // pass introduces, applied to raw linear radiance *before*
    // exposure/Reinhard -- a defensive guard against a theoretically-
    // impossible-but-unverified negative geometry-pass output. This
    // never reintroduces the removed pre-tonemap [0,1] clamp (ADR-0068
    // D-7) in a different guise -- any positive value, however large,
    // still reaches the curve unclipped.
    float3 linearColor = max(hdrColor.rgb, float3(0.0, 0.0, 0.0));
    float3 exposedColor = linearColor * kBaselineExposure;
    float3 tonemapped = exposedColor / (float3(1.0, 1.0, 1.0) + exposedColor);

    // No manual OETF here -- the *_Srgb final target's own hardware
    // encodes this linear value on store (ADR-0068 D-6). Alpha passes
    // through unchanged, matching output_transform_unorm.slang's own
    // identical rationale.
    return float4(tonemapped, hdrColor.a);
}
