// Plan 0066 P3 (ADR-0132 D3; Spec 0066 R4; J4): the selection outline's
// first pass -- each selected entity's mesh, at its current WorldMatrix, into
// a mask at the target's extent (red 1 where any selected mesh covers a
// pixel). No depth: the outline shows through occluders. The mesh's vertex
// buffer is read through its own stride; only the position is used.

struct VertexInput {
    [[vk::location(0)]] float3 position;
};

struct Camera {
    float4x4 view;
    float4x4 projection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Camera> camera;

struct PushConstants {
    float4x4 objectToWorld;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

[shader("vertex")]
float4 vertexMain(VertexInput input) : SV_Position {
    float4 worldPos = mul(pushConstants.objectToWorld, float4(input.position, 1.0));
    return mul(camera.projection, mul(camera.view, worldPos));
}

[shader("fragment")]
float4 fragmentMain() : SV_Target {
    return float4(1.0, 1.0, 1.0, 1.0);
}
