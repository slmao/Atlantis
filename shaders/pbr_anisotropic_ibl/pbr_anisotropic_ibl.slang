// Plan 0035 Milestone 4 (ADR-0081/Spec 0035 Requirement 3): PbrAnisotropic,
// IBL-only variant. Implements Filament's own published Anisotropic model
// (google.github.io/filament, Material System chapter, "Anisotropic
// model" section) for the roughness split, and an original,
// bent-reflection-vector IBL adaptation for the elongated/rotating
// highlight itself -- this is an original implementation citing that
// documented model, not copied source.
//
// Filament's own cited formulas (Kulla 2017, "Revisiting Physically
// Based Shading at Imageworks"), reproduced here exactly, drive the
// per-axis roughness split this shader actually uses:
//   at = roughness_alpha * (1 + anisotropy)
//   ab = roughness_alpha * (1 - anisotropy)
// where roughness_alpha = perceptualRoughness^2 (this codebase's own
// existing alpha-remap convention, matching pbr_clearcoat_ibl.slang's
// own identical clearcoatRoughnessAlpha computation), and `anisotropy`
// is anisotropyFactor, clamped to [-1, 1] per Spec 0035's own field
// definition. Filament's own Anisotropic model section additionally
// defines the direct-lighting D_GGX_Anisotropic(h, at, ab, ToH, BoH) and
// V_SmithGGXCorrelated_Anisotropic(at, ab, ToV, BoV, ToL, BoL, NoV, NoL)
// terms for evaluating a real light direction -- this Milestone's own
// disclosed IBL-only scope (matching PbrClearcoat/PbrSheen's own
// identical scope) has no direct light direction to evaluate those
// against, so instead (an ORIGINAL IBL adaptation, not documented by
// Filament's own material-system chapter under this same URL) this
// shader bends the shading normal toward the rotated tangent frame's
// own anisotropic-normal direction, by anisotropyFactor's own signed
// magnitude, before reflecting and sampling the SAME prefiltered
// environment cubemap the base layer already uses -- the standard
// "bent normal" IBL technique used by production anisotropic PBR
// renderers (Frostbite, Filament's own real engine source) for exactly
// this "no per-texel light direction to evaluate the real D/V terms
// against" situation. The mip/mip mip selected for that sample uses
// sqrt(sqrt(at * ab)) as an areal-equivalent perceptual roughness
// (matching the well-known sqrt(at*ab) "effective isotropic alpha"
// approximation for anisotropic environment prefiltering, then
// converted back to this codebase's own perceptual-roughness-keyed LOD
// convention).
//
// Tangent rotation (anisotropyRotation) happens entirely in the
// fragment stage, on the existing per-vertex tangent (Spec 0029/
// ADR-0073's own mandatory object-space tangent) -- no new vertex
// attribute, matching Spec 0035's own explicit "investigated, none"
// finding. Unlike PbrClearcoat/PbrSheen's own non-normal-map variants
// (which read only position/uv/normal), this variant's own VertexInput
// still carries the tangent (location 3) even though it never samples a
// normal map texture -- anisotropy inherently needs a tangent direction
// to rotate and bend around, regardless of whether a normal map is also
// present.

struct Varying {
    float4 position : SV_Position;
    float3 worldPosition;
    float3 worldNormal;
    float2 uv;
    float3 worldTangent;
    float tangentHandedness;
};

struct DirectionalLightGpu {
    float3 direction;
    float _pad0;
    float3 color;
    float intensity;
};

struct PointLightGpu {
    float3 position;
    float range;
    float3 color;
    float intensity;
};

struct CameraUniform {
    float4x4 view;
    float4x4 projection;
    uint directionalLightCount;
    uint pointLightCount;
    uint2 _pad1;
    DirectionalLightGpu directionalLights[1];
    // Plan 0040 (ADR-0088): 4 -> 64 point lights (kMaxPointLights); every
    // offset after this array moves +1920 bytes (cameraWorldPosition
    // 304 -> 2224, light-space pair 464 -> 2384, block 592 -> 2512).
    PointLightGpu pointLights[64];
    float3 cameraWorldPosition;
    float _pad2;
    float4 irradianceSh[9];
    float4x4 lightSpaceView;
    float4x4 lightSpaceProjection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera;

// Plan 0035 Milestone 4 (ADR-0081 Decision item 2): mirrors
// PbrAnisotropicPushConstants (src/renderer/src/pbr_anisotropic_push_constants.h)
// field-for-field -- confirmed by that struct's own static_asserts, not
// assumed here.
struct PushConstants {
    float4x4 objectToWorld;
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float anisotropyFactor;
    float anisotropyRotation;
    // Plan 0041 Milestone 2 (Spec 0041 R5): last field, as in the C++ struct.
    float3 emissiveFactor;
    // Plan 0042 Milestone 2 (Spec 0042 R6): the C++ struct's former tail pad.
    float alphaCutoff;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

struct VertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float3 normal;
    [[vk::location(3)]] float4 tangent;
};

[[vk::binding(1, 0)]]
Sampler2D baseColorSampler;

[[vk::binding(2, 0)]]
SamplerCube environmentSampler;

[[vk::binding(3, 0)]]
Sampler2D dfgLutSampler;

static const float kPi = 3.14159265358979323846;
static const float kMinDot = 1e-4;
static const float kSpecularMipCount = 9.0;
// Filament's own Anisotropic model section clamps alpha away from
// exactly 0 for the same reason PbrClearcoat's own
// kMinClearcoatPerceptualRoughness clamp exists -- a perfectly mirror
// axis still needs a well-defined, non-singular roughness value.
static const float kMinAlpha = 0.001;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    float4 worldPos4 = mul(pushConstants.objectToWorld, float4(input.position, 1.0));
    output.position = mul(camera.projection, mul(camera.view, worldPos4));
    output.worldPosition = worldPos4.xyz;
    output.worldNormal = mul((float3x3)pushConstants.objectToWorld, input.normal);
    output.uv = input.uv;
    output.worldTangent = mul((float3x3)pushConstants.objectToWorld, input.tangent.xyz);
    output.tangentHandedness = input.tangent.w;
    return output;
}

float3 evaluateIrradianceSH(float3 direction) {
    float x = direction.x;
    float y = direction.y;
    float z = direction.z;
    float basis[9] = {
        0.282095,
        0.488603 * z,
        0.488603 * y,
        0.488603 * x,
        1.092548 * x * z,
        1.092548 * z * y,
        0.315392 * (3.0 * y * y - 1.0),
        1.092548 * x * y,
        0.546274 * (x * x - z * z)
    };
    float3 result = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < 9; ++i) {
        result += camera.irradianceSh[i].rgb * basis[i];
    }
    return result;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float4 texColor = baseColorSampler.Sample(input.uv);
    float3 baseColor = texColor.rgb * pushConstants.baseColorFactor.rgb;
    float alphaOut = texColor.a * pushConstants.baseColorFactor.a;
    // Plan 0042 Milestone 2 (Spec 0042 R6): alpha test, before any lighting
    // work. The cutoff is 0 unless the material is Mask, and alpha >= 0,
    // so Opaque/Blend materials never discard.
    if (alphaOut < pushConstants.alphaCutoff) discard;

    float metallic = clamp(pushConstants.metallicFactor, 0.0, 1.0);
    float roughness = clamp(pushConstants.roughnessFactor, 0.0, 1.0);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    float3 N = normalize(input.worldNormal);
    // Plan 0029 Section P12 (ADR-0074 Section 5/6)'s own identical
    // fragment-stage TBN reconstruction -- unperturbed here (this
    // variant never samples a normal map), used only to build the
    // anisotropic tangent frame below.
    float3 T = normalize(input.worldTangent - N * dot(N, input.worldTangent));
    float3 B = cross(N, T) * input.tangentHandedness;

    // Fragment-stage tangent rotation (this file's own top comment, Spec
    // 0035's own explicit requirement) -- a standard 2D rotation within
    // the T/B tangent plane, N unchanged.
    float rotation = pushConstants.anisotropyRotation;
    float cosR = cos(rotation);
    float sinR = sin(rotation);
    float3 Tr = normalize(T * cosR + B * sinR);
    float3 Br = normalize(cross(N, Tr));

    float3 V = normalize(camera.cameraWorldPosition - input.worldPosition);
    float NdotV = max(dot(N, V), kMinDot);

    // Base metallic-roughness IBL layer -- identical math to
    // pbr_ibl.slang's own diffuseIbl term (the specular term below is
    // replaced by the anisotropic, bent-normal one).
    float3 Fibl = F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
    float3 kD = (float3(1.0, 1.0, 1.0) - Fibl) * (1.0 - metallic);
    float3 irradiance = max(evaluateIrradianceSH(N), float3(0.0, 0.0, 0.0));
    float3 diffuseIbl = kD * baseColor * irradiance / kPi;

    // Filament's own cited Kulla 2017 roughness split (this file's own
    // top comment) -- the real, substantive use of Filament's Anisotropic
    // model formula in this shader.
    float alpha = roughness * roughness;
    float anisotropy = clamp(pushConstants.anisotropyFactor, -1.0, 1.0);
    float at = max(alpha * (1.0 + anisotropy), kMinAlpha);
    float ab = max(alpha * (1.0 - anisotropy), kMinAlpha);

    // Bent-normal IBL adaptation (this file's own top comment): bends N
    // toward the rotated tangent frame's own anisotropic-normal
    // direction by anisotropy's own signed magnitude -- anisotropy = 0
    // leaves bentNormal exactly equal to N (degenerates to the isotropic
    // base case regardless of rotation), matching Spec 0035's own
    // required factor=0 sanity check.
    float3 anisotropicTangent = cross(Br, V);
    float3 anisotropicNormal = cross(anisotropicTangent, Br);
    float3 bentNormal = normalize(lerp(N, anisotropicNormal, anisotropy));

    float3 R = reflect(-V, bentNormal);
    float samplingPerceptualRoughness = sqrt(sqrt(at * ab));
    float lod = samplingPerceptualRoughness * (kSpecularMipCount - 1.0);
    float3 prefiltered = environmentSampler.SampleLevel(R, lod).rgb;
    float2 dfg = dfgLutSampler.Sample(float2(NdotV, samplingPerceptualRoughness)).rg;
    float3 specularIbl = prefiltered * (F0 * dfg.x + dfg.y);

    float3 accumulated = diffuseIbl + specularIbl;

    // Plan 0041 Milestone 2 (Spec 0041 R6, ADR-0089 Decision 3): self-lit
    // colour, added after every lighting term and before the output
    // transform -- in the return expression, so nothing can follow it.
    return float4(accumulated + pushConstants.emissiveFactor, alphaOut);
}
