// Plan 0035 Milestone 3 (ADR-0081/Spec 0035 Requirement 2): PbrSheen,
// IBL-only variant. A second, additive BRDF lobe over the same base
// metallic-roughness layer pbr_ibl.slang already implements,
// approximating cloth/fabric per Filament's own published Cloth model
// (google.github.io/filament, "Material system > Cloth model") -- this
// is an original, simplified IBL adaptation of that documented model,
// not copied source. Filament's own direct-lighting sheen term uses a
// specialized "Charlie" microfacet distribution (peaked at grazing
// angles) and a Neubelt visibility term, with sheenColor itself used
// directly as the lobe's own reflectance (no Fresnel-Schlick F0 blend,
// unlike the base layer) -- there is no equivalent pre-integrated
// Charlie/Neubelt environment BRDF LUT in this codebase (the existing
// DFG LUT is calibrated for GGX), so this IBL adaptation instead samples
// the SAME prefiltered environment cubemap the base layer already uses
// (at a mip level derived from sheenRoughness, the base layer's own
// convention), tints it directly by sheenColor (Filament's own "F =
// sheenColor" choice), and weights it by a Schlick-style grazing-angle
// term -- reproducing the Charlie distribution's own qualitative
// grazing-angle peak without literally evaluating it per-texel across
// an environment integral. Added on top of the base layer, never
// attenuating it (unlike PbrClearcoat's own energy-aware coat, sheen is
// not a physically separate coating and Spec 0035 prescribes no
// specific energy-compensation scheme for it).
//
// This Milestone's own explicit, disclosed scope (Spec 0035 Non-Goals):
// IBL-only, no direct (directional/point) lights, no shadow map. The
// CameraUniform struct below is still declared byte-for-byte identical
// to pbr_ibl.slang's own (including the light arrays and shadow
// matrices this shader never reads) -- see pbr_clearcoat_ibl.slang's
// own identical top-of-file comment for why.

struct Varying {
    float4 position : SV_Position;
    float3 worldPosition;
    float3 worldNormal;
    float2 uv;
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

// Plan 0035 Milestone 3 (ADR-0081 Decision item 2): mirrors
// PbrSheenPushConstants (src/renderer/src/pbr_sheen_push_constants.h)
// field-for-field -- confirmed by that struct's own static_asserts, not
// assumed here.
struct PushConstants {
    float4x4 objectToWorld;
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float3 sheenColor;
    float sheenRoughness;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

struct VertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float3 normal;
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

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    float4 worldPos4 = mul(pushConstants.objectToWorld, float4(input.position, 1.0));
    output.position = mul(camera.projection, mul(camera.view, worldPos4));
    output.worldPosition = worldPos4.xyz;
    output.worldNormal = mul((float3x3)pushConstants.objectToWorld, input.normal);
    output.uv = input.uv;
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

    float metallic = clamp(pushConstants.metallicFactor, 0.0, 1.0);
    float roughness = clamp(pushConstants.roughnessFactor, 0.0, 1.0);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    float3 N = normalize(input.worldNormal);
    float3 V = normalize(camera.cameraWorldPosition - input.worldPosition);
    float NdotV = max(dot(N, V), kMinDot);

    // Base metallic-roughness IBL layer -- identical math to
    // pbr_ibl.slang's own diffuseIbl/specularIbl terms.
    float3 Fibl = F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
    float3 kD = (float3(1.0, 1.0, 1.0) - Fibl) * (1.0 - metallic);
    float3 irradiance = max(evaluateIrradianceSH(N), float3(0.0, 0.0, 0.0));
    float3 diffuseIbl = kD * baseColor * irradiance / kPi;
    float3 R = reflect(-V, N);
    float lod = roughness * (kSpecularMipCount - 1.0);
    float3 prefiltered = environmentSampler.SampleLevel(R, lod).rgb;
    float2 dfg = dfgLutSampler.Sample(float2(NdotV, roughness)).rg;
    float3 specularIbl = prefiltered * (F0 * dfg.x + dfg.y);
    float3 baseLayer = diffuseIbl + specularIbl;

    // Sheen layer (this file's own top comment): sampled about the SAME
    // normal N the base layer uses (unlike PbrClearcoat's separate
    // coating, sheen describes this same surface's own microfiber
    // structure, not an independent layer above it), at a mip derived
    // from sheenRoughness, tinted directly by sheenColor (no F0 blend),
    // and weighted by a Schlick-style grazing-angle term standing in for
    // the Charlie distribution's own grazing-angle peak.
    float sheenRoughnessClamped = clamp(pushConstants.sheenRoughness, 0.0, 1.0);
    float sheenLod = sheenRoughnessClamped * (kSpecularMipCount - 1.0);
    float3 sheenPrefiltered = environmentSampler.SampleLevel(R, sheenLod).rgb;
    float sheenGrazing = pow(1.0 - NdotV, 5.0);
    float3 sheenLayer = pushConstants.sheenColor * sheenPrefiltered * sheenGrazing;

    float3 accumulated = baseLayer + sheenLayer;

    return float4(accumulated, alphaOut);
}
