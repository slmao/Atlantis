// A small, throwaway fixture shader for tests/tools/shader_compiler's
// own "tool"-labeled toolchain integration tests (Plan 0008 Section 9)
// -- NOT Minimal Renderer's own shader (that lives under
// shaders/minimal_renderer/ and is added by Plan 0008 Section 8's
// migration step). Deliberately shaped to match
// minimalRendererExpectedDescriptorContract() (one uniform buffer at
// set 0 / binding 0, vertex stage) and this round's fixed vertex-only
// push-constant expectation (offset 0, size 64), so it can also
// exercise compileAndValidate()'s full pipeline, not just a raw slangc
// invocation.

struct Varying {
    float4 position : SV_Position;
    float3 color;
};

struct CameraUniform {
    float4x4 view;
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
    [[vk::location(1)]] float3 color;
};

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = mul(camera.view, mul(pushConstants.objectToWorld, float4(input.position, 1.0)));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    return float4(input.color, 1.0);
}
