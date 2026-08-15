// Spec 0008 / Plan 0008 Section 8: replaces minimal_mesh.{vert,frag}.glsl
// with a single Slang module sharing one explicit varying-interface
// struct (ADR-0030's authoring convention) between the vertex and
// fragment entry points. Functionally equivalent to the retired GLSL
// pair -- camera uniform at [[vk::binding(0, 0)]], a push-constant
// object-to-world matrix, position/color vertex inputs at explicit
// [[vk::location(0)]]/[[vk::location(1)]], unlit per-vertex-color
// output. No lighting, no texture sample (Spec 0007's minimal-material
// Non-Goal, unchanged).

struct Varying {
    float4 position : SV_Position;
    float3 color;
};

struct CameraUniform {
    float4x4 view;
    float4x4 projection;
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
    output.position =
        mul(camera.projection, mul(camera.view, mul(pushConstants.objectToWorld, float4(input.position, 1.0))));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    return float4(input.color, 1.0);
}
