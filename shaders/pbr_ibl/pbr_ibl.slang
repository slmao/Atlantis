// Spec 0025/P3-P4: opt-in PBR direct lighting plus image-based lighting.
// The direct-light loops intentionally match pbr_direct_lit.slang; the
// separate shader keeps the no-environment path byte-for-byte unchanged.

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
    PointLightGpu pointLights[4];
    float3 cameraWorldPosition;
    float _pad2;
    float4 irradianceSh[9];
    // Plan 0027 Milestone 5 (ADR-0072 D-6): appended at the existing
    // tail (offset 464), mirroring Spec 0025 P3's own "appended after
    // the existing region, shaders only declare/read their own current
    // prefix" precedent. The one directional light's own light-space
    // view/projection pair -- identity when directionalLightCount == 0
    // (Runtime's own sentinel, never read in that case since the loop
    // below never executes).
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
};

[[vk::binding(1, 0)]]
Sampler2D baseColorSampler;

[[vk::binding(2, 0)]]
SamplerCube environmentSampler;

[[vk::binding(3, 0)]]
Sampler2D dfgLutSampler;

// Plan 0027 Milestone 5 (ADR-0072 D-5/D-7): the shadow map, at the next
// free binding after the three existing samplers.
[[vk::binding(4, 0)]]
Sampler2D shadowMapSampler;

static const float kPointLightDistanceEpsilon = 1e-4;
static const float kPi = 3.14159265358979323846;
static const float kMinAlpha = 1e-3;
static const float kMinDot = 1e-4;
static const float kSpecularMipCount = 9.0;
// Plan 0027 P4: a reasoned, not yet empirically-tuned, starting value --
// see Plan 0027's own P4 derivation.
static const float kShadowBias = 0.0015;

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

// Plan 0027 Milestone 5 (ADR-0072 D-5): a manual, non-hardware depth
// compare against the shadow map -- a fragment outside the shadow
// map's own fixed coverage volume is always treated as fully lit
// (never "always shadowed," which would visibly break every surface
// outside the fixed volume). Multiplies only the directional-light
// loop's own accumulated contribution below -- point lights and every
// IBL term outside this function's own call site are untouched.
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
    float alpha = max(roughness * roughness, kMinAlpha);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 diffuseColor = baseColor * (1.0 - metallic);

    float3 N = normalize(input.worldNormal);
    float3 V = normalize(camera.cameraWorldPosition - input.worldPosition);
    float3 accumulated = float3(0.0, 0.0, 0.0);

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

            // Plan 0027 Milestone 5 (ADR-0072 D-5): multiplies only this
            // loop's own accumulated contribution -- point lights and
            // every IBL term below are untouched.
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
    accumulated += diffuseIbl + specularIbl;

    return float4(accumulated, alphaOut);
}
