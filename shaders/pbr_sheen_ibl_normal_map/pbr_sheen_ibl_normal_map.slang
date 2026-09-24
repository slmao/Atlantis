// Plan 0035 Milestone 3 (ADR-0081/Spec 0035 Requirement 2): PbrSheen,
// IBL + normal-map variant -- pbr_sheen_ibl.slang's own exact twin, plus
// tangent-space normal mapping (mirrors pbr_ibl_normal_map.slang's own
// identical addition over pbr_ibl.slang). Unlike PbrClearcoat's own
// normal-map variant (whose coat lobe deliberately keeps reflecting
// about the unperturbed geometric normal), the sheen lobe here uses the
// SAME normal-mapped N as the base layer -- see pbr_sheen_ibl.slang's
// own top comment for why sheen is treated as describing this same
// surface, not an independent coating.

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

// Plan 0043 Milestone 2 (Spec 0043 R4, ADR-0091 Decision 3): the camera
// uniform's last region, mirroring scene_extraction.h's FogData --
// density shares color's 16-byte slot; 32 bytes at offset 2512.
struct FogData {
    float3 color;
    float density;
    float height;
    float heightFalloff;
    float maxOpacity;
    float _pad;
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
    // Plan 0043: density 0 means fog off (every writer writes this tail).
    FogData fog;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera;

struct PushConstants {
    float4x4 objectToWorld;
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float3 sheenColor;
    float sheenRoughness;
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

// Plan 0035 Milestone 3 (ADR-0081): the normal map, at the next free
// binding (4) -- mirrors pbr_clearcoat_ibl_normal_map.slang's own
// identical binding-4 placement (no shadow-map binding sits between DFG
// LUT (3) and this one, unlike pbr_ibl_normal_map.slang's own binding 5).
[[vk::binding(4, 0)]]
Sampler2D normalMapSampler;

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

// Plan 0043 Milestone 2 (Spec 0043 R1, ADR-0091 Decision 2): analytic
// exponential height fog. Duplicated verbatim in all ten PBR shaders
// (ruling Q5) and mirrored by tests/image_regression/support/
// fog_reference.h, which the GPU tests check these shaders against.
// Below kFogSeriesThreshold, g(x) = (1 - e^-x) / x uses its Taylor
// series: the closed form cancels near 0 and is 0/0 at 0 (Plan 0043 P6).
static const float kFogSeriesThreshold = 1e-3;

float fogHeightIntegral(float x) {
    if (abs(x) < kFogSeriesThreshold) {
        return 1.0 - x / 2.0 + x * x / 6.0;
    }
    return (1.0 - exp(-x)) / x;
}

// tau = density * d * e^(-heightFalloff * h) * g(heightFalloff * dy);
// f = min(1 - e^-tau, maxOpacity); rgb = lit * (1 - f) + color * f.
float3 applyHeightFog(float3 lit, float3 worldPosition) {
    float3 toPoint = worldPosition - camera.cameraWorldPosition;
    float h = camera.cameraWorldPosition.y - camera.fog.height;
    float tau = camera.fog.density * length(toPoint) * exp(-camera.fog.heightFalloff * h) *
                fogHeightIntegral(camera.fog.heightFalloff * toPoint.y);
    float f = min(1.0 - exp(-tau), camera.fog.maxOpacity);
    return lit * (1.0 - f) + camera.fog.color * f;
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

    // Plan 0029 Section P12 (ADR-0074 Section 5/6)'s own identical
    // fragment-stage TBN reconstruction.
    float3 N_geo = normalize(input.worldNormal);
    float3 T = normalize(input.worldTangent - N_geo * dot(N_geo, input.worldTangent));
    float3 B = cross(N_geo, T) * input.tangentHandedness;
    float3 texelN = normalMapSampler.Sample(input.uv).rgb * 2.0 - 1.0;
    float3 N = normalize(texelN.x * T + texelN.y * B + texelN.z * N_geo);

    float3 V = normalize(camera.cameraWorldPosition - input.worldPosition);
    float NdotV = max(dot(N, V), kMinDot);

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

    // Sheen layer -- uses the SAME normal-mapped N/R as the base layer
    // (this file's own top comment).
    float sheenRoughnessClamped = clamp(pushConstants.sheenRoughness, 0.0, 1.0);
    float sheenLod = sheenRoughnessClamped * (kSpecularMipCount - 1.0);
    float3 sheenPrefiltered = environmentSampler.SampleLevel(R, sheenLod).rgb;
    float sheenGrazing = pow(1.0 - NdotV, 5.0);
    float3 sheenLayer = pushConstants.sheenColor * sheenPrefiltered * sheenGrazing;

    float3 accumulated = baseLayer + sheenLayer;

    // Plan 0041 Milestone 2 (Spec 0041 R6, ADR-0089 Decision 3): self-lit
    // colour, added after every lighting term and before the output
    // transform. Plan 0043 (Spec 0043 R2-R3): height fog is the one term
    // after it, on RGB only, behind a dynamically uniform branch rather
    // than a zero multiply -- density 0 must skip the exp()s entirely
    // (0 * inf would be NaN), so fog off is the unfogged value bit for bit.
    float3 color = accumulated + pushConstants.emissiveFactor;
    if (camera.fog.density > 0.0) {
        color = applyHeightFog(color, input.worldPosition);
    }
    return float4(color, alphaOut);
}
