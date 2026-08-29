// Plan 0019 Section P11 (Spec 0019 D6/D7/D8): a real combined image
// sampler binding, exactly like textured_quad.slang already has, plus
// the one-time frame lighting data (ADR-0062 D1/D5) read from the
// existing camera uniform binding's own newly-fragment-visible stage
// (ADR-0062 Decision 2). The CameraUniform layout below mirrors
// plans/0019-lighting-foundation.md P7's own single authoritative field
// table field-for-field, offset-for-offset -- Milestone 6's own real
// Slang reflection is the actual verification of that claim, not this
// comment alone.

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
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera;

struct PushConstants {
    float4x4 objectToWorld;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

struct VertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float3 normal;
};

[[vk::binding(1, 0)]]
Sampler2D texturedSampler;

// Spec 0019 D6: a documented, named constant -- never a bare literal.
static const float kPointLightDistanceEpsilon = 1e-4;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    float4 worldPos4 = mul(pushConstants.objectToWorld, float4(input.position, 1.0));
    output.position = mul(camera.projection, mul(camera.view, worldPos4));
    output.worldPosition = worldPos4.xyz;
    // Spec 0019 D7: direct transformation by the object-to-world
    // matrix's own upper-left 3x3 -- correct exactly when that
    // submatrix is conformal (Runtime's own checkConformalTransform()
    // gates which entities ever reach this shader with a non-conformal
    // transform, plans/0019-lighting-foundation.md P8/P15).
    output.worldNormal = mul((float3x3)pushConstants.objectToWorld, input.normal);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float4 texColor = texturedSampler.Sample(input.uv);
    // Spec 0019 D6: N = normalize(worldNormal) -- interpolation across a
    // triangle does not preserve unit length.
    float3 N = normalize(input.worldNormal);
    float3 accumulated = float3(0.0, 0.0, 0.0);  // no ambient term -- explicit, see D6

    for (uint i = 0; i < camera.directionalLightCount; ++i) {
        float3 L = -camera.directionalLights[i].direction;
        float ndotl = max(dot(N, L), 0.0);
        accumulated += camera.directionalLights[i].color * camera.directionalLights[i].intensity * ndotl;
    }

    for (uint j = 0; j < camera.pointLightCount; ++j) {
        float3 toLight = camera.pointLights[j].position - input.worldPosition;
        float dist = max(length(toLight), kPointLightDistanceEpsilon);
        float3 L = toLight / dist;
        float ndotl = max(dot(N, L), 0.0);
        // Spec 0019 D6: explicitly non-physical, linear falloff -- zero
        // at range, one at dist == 0.
        float atten = clamp(1.0 - dist / camera.pointLights[j].range, 0.0, 1.0);
        accumulated += camera.pointLights[j].color * camera.pointLights[j].intensity * ndotl * atten;
    }

    // The one and only clamp, applied here, after combining texture and
    // light -- never per-light. No tone-mapping, no gamma-encode.
    float3 finalRgb = clamp(texColor.rgb * accumulated, 0.0, 1.0);
    return float4(finalRgb, texColor.a);
}
