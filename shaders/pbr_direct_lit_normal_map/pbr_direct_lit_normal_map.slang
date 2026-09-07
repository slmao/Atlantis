// Plan 0029 Section P12 (ADR-0074 Section 5/6): pbr_direct_lit.slang's
// own exact twin, plus tangent-space normal mapping. VertexInput gains
// a fourth location, the tangent attribute (ADR-0073); a new
// normalMapSampler binding (3) reads the stored RGB8 Unorm texel,
// mapped [0,1] -> [-1,1] per channel (OpenGL-style, +Y-up convention,
// no channel ever flipped). The perturbed normal N replaces N_geo
// everywhere the existing BRDF math already reads it -- no other line
// of this shader's own accumulation math changes from
// pbr_direct_lit.slang.

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
    PointLightGpu pointLights[4];
    float3 cameraWorldPosition;
    float _pad2;
    float4 _shadowPad[9];
    float4x4 lightSpaceView;
    float4x4 lightSpaceProjection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera;

struct PushConstants {
    float4x4 objectToWorld;
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float2 _pad;
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
Sampler2D shadowMapSampler;

// Plan 0029 Section P12 (ADR-0074 Section 5): the normal map, at the
// next free binding after the shadow map.
[[vk::binding(3, 0)]]
Sampler2D normalMapSampler;

static const float kPointLightDistanceEpsilon = 1e-4;
static const float kPi = 3.14159265358979323846;
static const float kMinAlpha = 1e-3;
static const float kMinDot = 1e-4;
static const float kShadowBias = 0.0015;

float computeShadowFactor(float3 worldPosition) {
    float4 shadowClip = mul(camera.lightSpaceProjection, mul(camera.lightSpaceView, float4(worldPosition, 1.0)));
    float3 shadowNdc = shadowClip.xyz / shadowClip.w;
    float2 shadowUv = shadowNdc.xy * 0.5 + 0.5;
    bool inBounds = all(shadowUv >= float2(0.0, 0.0)) && all(shadowUv <= float2(1.0, 1.0)) &&
                     shadowNdc.z >= 0.0 && shadowNdc.z <= 1.0;
    if (!inBounds) {
        return 1.0;
    }
    float storedDepth = shadowMapSampler.Sample(shadowUv).r;
    return (shadowNdc.z - kShadowBias) <= storedDepth ? 1.0 : 0.0;
}

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    float4 worldPos4 = mul(pushConstants.objectToWorld, float4(input.position, 1.0));
    output.position = mul(camera.projection, mul(camera.view, worldPos4));
    output.worldPosition = worldPos4.xyz;
    output.worldNormal = mul((float3x3)pushConstants.objectToWorld, input.normal);
    output.uv = input.uv;
    // Plan 0029 Section P12 (ADR-0074 Section 5): the same
    // submatrix/gate the existing normal transform above already
    // uses -- never a different transform path.
    output.worldTangent = mul((float3x3)pushConstants.objectToWorld, input.tangent.xyz);
    output.tangentHandedness = input.tangent.w;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float4 texColor = baseColorSampler.Sample(input.uv);
    float3 baseColor = texColor.rgb * pushConstants.baseColorFactor.rgb;
    float alphaOut = texColor.a * pushConstants.baseColorFactor.a;

    float metallic = clamp(pushConstants.metallicFactor, 0.0, 1.0);
    float roughness = clamp(pushConstants.roughnessFactor, 0.0, 1.0);
    float alpha = max(roughness * roughness, kMinAlpha);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 diffuseColor = baseColor * (1.0 - metallic);

    // Plan 0029 Section P12 (ADR-0074 Section 5/6): a second,
    // fragment-stage Gram-Schmidt pass (per-vertex interpolation
    // across a triangle does not preserve exact orthogonality),
    // independent of ADR-0073's own cook-time one. B = cross(N_geo, T)
    // * handedness -- tangentHandedness is ADR-0073's own cook-time
    // value, never re-derived here. The stored RGB8 Unorm texel is
    // mapped [0,1] -> [-1,1] per channel directly -- no channel is
    // ever flipped (OpenGL, +Y-up convention).
    float3 N_geo = normalize(input.worldNormal);
    float3 T = normalize(input.worldTangent - N_geo * dot(N_geo, input.worldTangent));
    float3 B = cross(N_geo, T) * input.tangentHandedness;
    float3 texelN = normalMapSampler.Sample(input.uv).rgb * 2.0 - 1.0;
    float3 N = normalize(texelN.x * T + texelN.y * B + texelN.z * N_geo);

    float3 V = normalize(camera.cameraWorldPosition - input.worldPosition);
    float3 accumulated = float3(0.0, 0.0, 0.0);  // no ambient term, matching every other MaterialKind

    for (uint i = 0; i < camera.directionalLightCount; ++i) {
        float3 L = -camera.directionalLights[i].direction;
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            float3 radiance = camera.directionalLights[i].color * camera.directionalLights[i].intensity;
            float NdotV = max(dot(N, V), kMinDot);
            float3 H = normalize(L + V);
            float NdotH = max(dot(N, H), 0.0);
            float VdotH = max(dot(V, H), 0.0);

            float D = (alpha * alpha) / (kPi * pow(NdotH * NdotH * (alpha * alpha - 1.0) + 1.0, 2.0));

            float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
            float G1V = NdotV / (NdotV * (1.0 - k) + k);
            float G1L = NdotL / (NdotL * (1.0 - k) + k);
            float G = G1V * G1L;

            float3 F = F0 + (float3(1.0, 1.0, 1.0) - F0) * pow(1.0 - VdotH, 5.0);

            float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, kMinDot);
            float3 kD = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);

            float shadowFactor = computeShadowFactor(input.worldPosition);
            accumulated += shadowFactor * (kD * diffuseColor / kPi + specular) * radiance * NdotL;
        }
    }

    for (uint j = 0; j < camera.pointLightCount; ++j) {
        float3 toLight = camera.pointLights[j].position - input.worldPosition;
        float dist = max(length(toLight), kPointLightDistanceEpsilon);
        float3 L = toLight / dist;
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            float atten = clamp(1.0 - dist / camera.pointLights[j].range, 0.0, 1.0);
            float3 radiance = camera.pointLights[j].color * camera.pointLights[j].intensity * atten;
            float NdotV = max(dot(N, V), kMinDot);
            float3 H = normalize(L + V);
            float NdotH = max(dot(N, H), 0.0);
            float VdotH = max(dot(V, H), 0.0);

            float D = (alpha * alpha) / (kPi * pow(NdotH * NdotH * (alpha * alpha - 1.0) + 1.0, 2.0));

            float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
            float G1V = NdotV / (NdotV * (1.0 - k) + k);
            float G1L = NdotL / (NdotL * (1.0 - k) + k);
            float G = G1V * G1L;

            float3 F = F0 + (float3(1.0, 1.0, 1.0) - F0) * pow(1.0 - VdotH, 5.0);

            float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, kMinDot);
            float3 kD = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);

            accumulated += (kD * diffuseColor / kPi + specular) * radiance * NdotL;
        }
    }

    return float4(accumulated, alphaOut);
}
