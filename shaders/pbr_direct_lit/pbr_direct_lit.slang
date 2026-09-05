// Plan 0023 Milestone 4 (ADR-0067 D-1/D-2, ADR-0062's own Accepted
// Amendment): a metallic-roughness Cook-Torrance BRDF (Lambertian
// diffuse + GGX/Smith-Schlick specular) for Directional and Point
// lights, sharing lit_textured.slang's own vertex-input shape and one
// combined-image-sampler binding. The CameraUniform layout below
// mirrors Milestone 2's own real C++ field-for-field, offset-for-offset
// (CameraMatrices unchanged, FrameLightingData unchanged,
// CameraWorldPositionData appended at the tail, offset 304) --
// Milestone 7's own real Slang reflection cross-check is the actual
// verification of that claim, not this comment alone. PushConstants
// mirrors PbrPushConstants (Milestone 3) exactly.
//
// Plan 0024 Milestone 4 (ADR-0068 D-7): the former "final clamp is the
// only transformation" output contract no longer applies to this
// shader -- its own final clamp is removed; fragmentMain() now returns
// scene-referred linear HDR, written into a caller-owned HdrColorTarget
// instead of the final presentable target. Tone-mapping and the
// display transfer function now happen once, downstream, in the new
// output-transform pass -- see shaders/output_transform_unorm/ and
// shaders/output_transform_srgb/.

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
    // ADR-0062's own Accepted Amendment (Milestone 2): tail-only,
    // appended after the unchanged 304-byte Camera+Lighting region.
    // Explicit tail pad, matching CameraWorldPositionData's own C++
    // _pad convention -- not compiler-implicit.
    float3 cameraWorldPosition;
    float _pad2;
    // Plan 0027 Milestone 5 (ADR-0072 D-6): this shader stops at byte
    // 320 -- pbr_ibl.slang's own irradianceSh[9] reaches byte 464
    // instead. This explicit, named 144-byte pad (matching this
    // codebase's own "explicit padding, never implicit" discipline)
    // keeps the light-space tail below at the identical 464-byte offset
    // in both shaders, rather than two diverging ones.
    float4 _shadowPad[9];
    // The one directional light's own light-space view/projection pair
    // -- identity when directionalLightCount == 0 (Runtime's own
    // sentinel, never read in that case since the loop below never
    // executes).
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

// Plan 0027 Milestone 5 (ADR-0072 D-5/D-7): the shadow map, at the next
// free binding after the one existing sampler.
[[vk::binding(2, 0)]]
Sampler2D shadowMapSampler;

// Spec 0019 D6's own precedent, reused verbatim for the point-light
// distance floor. ADR-0067 D-1/D-2's own new constants for the BRDF
// itself.
static const float kPointLightDistanceEpsilon = 1e-4;
static const float kPi = 3.14159265358979323846;
static const float kMinAlpha = 1e-3;
static const float kMinDot = 1e-4;
// Plan 0027 P4: a reasoned, not yet empirically-tuned, starting value --
// see Plan 0027's own P4 derivation.
static const float kShadowBias = 0.0015;

// Plan 0027 Milestone 5 (ADR-0072 D-5): a manual, non-hardware depth
// compare against the shadow map -- a fragment outside the shadow
// map's own fixed coverage volume is always treated as fully lit
// (never "always shadowed," which would visibly break every surface
// outside the fixed volume). Multiplies only the directional-light
// loop's own accumulated contribution below -- point lights are
// untouched (this shader has no IBL term at all).
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
    // Spec 0019 D7's own precedent, reused verbatim: correct exactly
    // when the object-to-world submatrix is conformal (Runtime's own
    // checkConformalTransform() gates which entities ever reach this
    // shader with a non-conformal transform).
    output.worldNormal = mul((float3x3)pushConstants.objectToWorld, input.normal);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float4 texColor = baseColorSampler.Sample(input.uv);
    // Rgba8Srgb source, hardware-decoded to linear at sample time
    // (ADR-0066) -- baseColorFactor is itself an already-linear
    // authoring-time multiplier (Spec 0023/ADR-0066), so this product
    // is linear-space throughout, matching every other MaterialKind's
    // own lighting math in this engine.
    float3 baseColor = texColor.rgb * pushConstants.baseColorFactor.rgb;
    float alphaOut = texColor.a * pushConstants.baseColorFactor.a;  // stored, currently inert -- no blending anywhere

    float metallic = clamp(pushConstants.metallicFactor, 0.0, 1.0);
    float roughness = clamp(pushConstants.roughnessFactor, 0.0, 1.0);
    float alpha = max(roughness * roughness, kMinAlpha);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 diffuseColor = baseColor * (1.0 - metallic);

    float3 N = normalize(input.worldNormal);
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

            // Karis's own DIRECT-LIGHTING remap, using roughness
            // directly -- never alpha (alpha/2 is his IBL remap, not
            // used here).
            float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
            float G1V = NdotV / (NdotV * (1.0 - k) + k);
            float G1L = NdotL / (NdotL * (1.0 - k) + k);
            float G = G1V * G1L;

            float3 F = F0 + (float3(1.0, 1.0, 1.0) - F0) * pow(1.0 - VdotH, 5.0);

            float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, kMinDot);
            float3 kD = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);

            // Plan 0027 Milestone 5 (ADR-0072 D-5): multiplies only this
            // loop's own accumulated contribution -- the point-light
            // loop below is untouched.
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
            // Spec 0019 D6's own precedent, reused verbatim: explicitly
            // non-physical, linear falloff -- zero at range, one at
            // dist == 0.
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

    // Plan 0024 Milestone 4 (ADR-0068 D-7): the former "one and only
    // clamp, applied here" is removed -- this shader's own output is
    // now scene-referred linear HDR, written into a caller-owned
    // HdrColorTarget (Rgba16Float) instead of the final presentable
    // target. Clamping to [0,1] here would defeat the entire purpose of
    // the HDR intermediate: a bright highlight above 1.0 must survive
    // to reach the output-transform pass's own tone-mapping curve, not
    // be pre-clipped away. No other line in this function changes.
    return float4(accumulated, alphaOut);
}
