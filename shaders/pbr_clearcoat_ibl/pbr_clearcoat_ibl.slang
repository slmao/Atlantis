// Plan 0035 Milestone 2 (ADR-0081/Spec 0035 Requirement 1): PbrClearcoat,
// IBL-only variant. A second, independent BRDF lobe over the same base
// metallic-roughness layer pbr_ibl.slang already implements, following
// Filament's own published Clear coat model (google.github.io/filament,
// "Material system > Clear coat model") -- this is an original
// implementation of that documented model, not copied source.
//
// This Milestone's own explicit, disclosed scope (Spec 0035 Non-Goals):
// IBL-only, no direct (directional/point) lights, no shadow map. The
// CameraUniform struct below is still declared byte-for-byte identical
// to pbr_ibl.slang's own (including the light arrays and shadow
// matrices this shader never reads) -- it is bound to the SAME shared
// per-frame uniform buffer every PBR shader reads from, so every field
// up to and including the ones this shader actually uses
// (irradianceSh, cameraWorldPosition, view/projection) must sit at the
// identical byte offset that buffer's own real, single layout already
// fixes; declaring a shorter/reordered struct here would silently
// misread it.

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

// Plan 0035 Milestone 2 (ADR-0081 Decision item 2): mirrors
// PbrClearcoatPushConstants (src/renderer/src/pbr_clearcoat_push_constants.h)
// field-for-field -- 96 bytes total, confirmed by that struct's own
// static_asserts, not assumed here.
struct PushConstants {
    float4x4 objectToWorld;
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float clearcoatFactor;
    float clearcoatRoughness;
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
// Filament's own clear coat model fixes the coat's own IOR at 1.5,
// giving F0 = ((1.5-1)/(1.5+1))^2 = 0.04 -- the same dielectric F0
// value the base layer's own F0 lerp already uses at metallic = 0.
static const float kClearcoatF0 = 0.04;
// Filament's own clear coat model clamps perceptual roughness away from
// exactly 0 (a mirror coat still needs a well-defined, non-singular
// specular lobe) -- 0.089 is Filament's own published minimum.
static const float kMinClearcoatPerceptualRoughness = 0.089;

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

    // Clear coat layer (Filament's own Clear coat model): a second,
    // independent GGX-IBL specular lobe, fixed F0 = 0.04 (never tinted
    // by the base layer's own metalness -- a clear coat is always a
    // dielectric), using clearcoatRoughness for its own roughness.
    // Reflects about the SAME geometric normal N (no separate clearcoat
    // normal map on this variant -- see pbr_clearcoat_ibl_normal_map.slang
    // for the one that perturbs N for the base layer only, matching
    // Filament's own "clear coat normal defaults to the geometric
    // normal unless a dedicated clear coat normal map is supplied"
    // convention -- this Milestone does not add a SEPARATE clearcoat-
    // only normal map input, only the existing base normalMapTexture).
    float clearcoatPerceptualRoughness =
        clamp(pushConstants.clearcoatRoughness, kMinClearcoatPerceptualRoughness, 1.0);
    float clearcoatRoughnessAlpha = clearcoatPerceptualRoughness * clearcoatPerceptualRoughness;
    float clearcoatLod = clearcoatRoughnessAlpha * (kSpecularMipCount - 1.0);
    float3 clearcoatPrefiltered = environmentSampler.SampleLevel(R, clearcoatLod).rgb;
    float2 clearcoatDfg = dfgLutSampler.Sample(float2(NdotV, clearcoatRoughnessAlpha)).rg;
    float3 clearcoatSpecularIbl = clearcoatPrefiltered * (kClearcoatF0 * clearcoatDfg.x + clearcoatDfg.y);

    // Base-layer energy attenuation by the coat's own Fresnel term
    // (Filament's own model: Fc = F_Schlick(0.04, NdotV) * clearCoat),
    // then the coat's own contribution added on top, scaled by
    // clearcoatFactor -- both directions of Filament's own documented
    // "(1 - Fc) * base + Fc * coat"-shaped energy split.
    float clearcoatFactor = clamp(pushConstants.clearcoatFactor, 0.0, 1.0);
    float Fc = kClearcoatF0 + (1.0 - kClearcoatF0) * pow(1.0 - NdotV, 5.0);
    Fc *= clearcoatFactor;
    float3 accumulated = baseLayer * (1.0 - Fc) + clearcoatSpecularIbl * Fc;

    return float4(accumulated, alphaOut);
}
