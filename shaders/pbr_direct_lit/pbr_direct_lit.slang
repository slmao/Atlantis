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
    // Plan 0043: density 0 means fog off (every writer writes this tail).
    FogData fog;
    // Plan 0061 Stage 2 (ADR-0122 D2): the cluster light masks after FogData, mirroring
    // scene_extraction.h's ClusterLightData -- cluster c's 64-bit mask is clusterMasks[c / 2].xy
    // (c even) or .zw (c odd); grid 12x7x16 = 1344 clusters, 672 uint4.
    uint4 clusterGrid;
    float4 clusterSlicing;
    uint4 clusterMasks[672];
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera;

struct PushConstants {
    float4x4 objectToWorld;
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float2 _pad;
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
};

[[vk::binding(1, 0)]]
Sampler2D baseColorSampler;

// Plan 0027 Milestone 5 (ADR-0072 D-5/D-7): the shadow map, at the next
// free binding after the one existing sampler.
[[vk::binding(2, 0)]]
Sampler2D shadowMapSampler;

// Plan 0046 Milestone 1 (ADR-0096): the emissive texture, always bound
// (the Runtime's 1x1 white default when the material names none), at the
// next free binding; sampled through the base-colour sampler state.
[[vk::binding(3, 0)]]
Sampler2D emissiveSampler;

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

// BEGIN cofactorNormal -- ADR-0134 D1 (Spec 0067 R1). Byte-identical in all
// eleven lit vertex shaders, held to tests/renderer/cofactor_normal_reference.slang
// by tests/renderer/cofactor_normal_tests.cpp; change all twelve together.
// The normal's direction under any invertible world 3x3 M is
// sign(det M) * cof(M) * n, where cof(M) = [c1 x c2 | c2 x c0 | c0 x c1] =
// det(M) * transpose(inverse(M)) for M's columns c0, c1, c2. For a conformal
// M (rotation times uniform scale) it has the direction of M * n.
float3 cofactorNormal(float4x4 objectToWorld, float3 n) {
    float3x3 columns = transpose((float3x3)objectToWorld);
    float3 c0 = columns[0];
    float3 c1 = columns[1];
    float3 c2 = columns[2];
    float3 c1xc2 = cross(c1, c2);
    float3 normal = c1xc2 * n.x + cross(c2, c0) * n.y + cross(c0, c1) * n.z;
    return dot(c0, c1xc2) < 0.0 ? -normal : normal;
}
// END cofactorNormal

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    float4 worldPos4 = mul(pushConstants.objectToWorld, float4(input.position, 1.0));
    output.position = mul(camera.projection, mul(camera.view, worldPos4));
    output.worldPosition = worldPos4.xyz;
    // ADR-0134 D1 (Spec 0067; superseding Spec 0019 D7): the normal by
    // the object-to-world 3x3's cofactor, correct for any invertible
    // world matrix.
    output.worldNormal = cofactorNormal(pushConstants.objectToWorld, input.normal);
    output.uv = input.uv;
    return output;
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

// Plan 0061 Stage 2 (Spec 0061 Revision 1 S2-R4, ADR-0122 D3): the cluster of
// a world position, from the frame's own view and projection (extent-
// independent). Integer clamps keep the index in range for any input,
// including a helper lane's extrapolated position.
uint computeClusterIndex(float3 worldPosition) {
    float4 viewPosition = mul(camera.view, float4(worldPosition, 1.0));
    float4 clip = mul(camera.projection, viewPosition);
    float2 ndc = clip.xy / clip.w;
    int gx = int(camera.clusterGrid.x);
    int gy = int(camera.clusterGrid.y);
    int gz = int(camera.clusterGrid.z);
    int x = clamp(int(floor((ndc.x * 0.5 + 0.5) * float(gx))), 0, gx - 1);
    int y = clamp(int(floor((ndc.y * 0.5 + 0.5) * float(gy))), 0, gy - 1);
    float viewZ = -viewPosition.z;
    int z = 0;
    if (viewZ >= camera.clusterSlicing.y) {
        z = clamp(1 + int(floor(log(viewZ / camera.clusterSlicing.y) * camera.clusterSlicing.w)), 1, gz - 1);
    }
    return uint(x + gx * (y + gy * z));
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
    float alphaOut = texColor.a * pushConstants.baseColorFactor.a;
    // Plan 0042 Milestone 2 (Spec 0042 R6): alpha test, before any lighting
    // work. The cutoff is 0 unless the material is Mask, and alpha >= 0,
    // so Opaque/Blend materials never discard.
    if (alphaOut < pushConstants.alphaCutoff) discard;

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

    // Plan 0061 Stage 2 (ADR-0122 D3): only the lights that may reach this fragment's cluster, in
    // ascending index (low word, then high word, low bit first); the body is Stage 1's, unchanged.
    uint clusterIndex = computeClusterIndex(input.worldPosition);
    uint4 clusterMaskPair = camera.clusterMasks[clusterIndex >> 1];
    uint2 pointLightMask = (clusterIndex & 1u) != 0u ? clusterMaskPair.zw : clusterMaskPair.xy;
    for (uint maskWord = 0; maskWord < 2; ++maskWord) {
        uint maskBits = maskWord == 0 ? pointLightMask.x : pointLightMask.y;
        while (maskBits != 0u) {
            uint j = maskWord * 32u + firstbitlow(maskBits);
            maskBits &= maskBits - 1u;
            float3 toLight = camera.pointLights[j].position - input.worldPosition;
            float dist = max(length(toLight), kPointLightDistanceEpsilon);
            float3 L = toLight / dist;
            float NdotL = max(dot(N, L), 0.0);
            // Spec 0019 D6's own precedent, reused verbatim: explicitly
            // non-physical, linear falloff -- zero at range, one at
            // dist == 0.
            float atten = clamp(1.0 - dist / camera.pointLights[j].range, 0.0, 1.0);
            // Plan 0061 M3 (Spec 0061 R3, ADR-0121 D1): the BRDF only where atten > 0 --
            // a skipped light's term would be exactly +0, so the sum is bit-identical.
            if (NdotL > 0.0 && atten > 0.0) {
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
    }

    // Plan 0024 Milestone 4 (ADR-0068 D-7): the former "one and only
    // clamp, applied here" is removed -- this shader's own output is
    // now scene-referred linear HDR, written into a caller-owned
    // HdrColorTarget (Rgba16Float) instead of the final presentable
    // target. Clamping to [0,1] here would defeat the entire purpose of
    // the HDR intermediate: a bright highlight above 1.0 must survive
    // to reach the output-transform pass's own tone-mapping curve, not
    // be pre-clipped away. No other line in this function changes.
    // Plan 0041 Milestone 2 (Spec 0041 R6, ADR-0089 Decision 3): self-lit
    // colour, added after every lighting term and before the output
    // transform. Plan 0043 (Spec 0043 R2-R3): height fog is the one term
    // after it, on RGB only, behind a dynamically uniform branch rather
    // than a zero multiply -- density 0 must skip the exp()s entirely
    // (0 * inf would be NaN), so fog off is the unfogged value bit for bit.
    // Plan 0046 Milestone 1 (ADR-0096): the factor scaled by the emissive
    // texture -- white x factor is the factor exactly.
    float3 color = accumulated + pushConstants.emissiveFactor * emissiveSampler.Sample(input.uv).rgb;
    if (camera.fog.density > 0.0) {
        color = applyHeightFog(color, input.worldPosition);
    }
    return float4(color, alphaOut);
}
