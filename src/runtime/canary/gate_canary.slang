// Plan 0060 (Spec 0060 Correction 2026-10-15, GPU-state canary): the gate's fixed GPU micro-workload.
//
// Three fragment workloads that share one vertex stage, each stressing a different resource, so a change in the
// machine's GPU state (clocks, memory bandwidth) shows in at least one of them whichever resource it is:
//   fragmentAlu - ALU bound: a long dependent mad/frac chain per fragment, no memory traffic.
//   fragmentBw  - write bandwidth: cheap fragments; the host draws the fullscreen triangle many times into an
//                 RGBA16F target (overdraw).
//   fragmentTex - read bandwidth and latency: 16 dependent, pseudo-random fetches from a target far larger than
//                 any cache (the RGBA16F target the bandwidth pass just wrote).
//
// The workload is fixed: this file, the draw counts and the target sizes in canary_run.cpp are the canary, and
// none of it is shared with the renderer, so a renderer change cannot move it. It is compiled once and embedded
// as SPIR-V in canary_spirv.h (see that file for the commands).

struct Varying {
    float4 position : SV_Position;
    float2 uv;
};

struct VertexInput {
    [[vk::location(0)]] float2 clipPosition;
};

struct CanaryPush {
    float seed;   // differs per draw so no draw can be elided as a repeat
    float count;  // fragmentAlu: loop trip count (a uniform, so the loop cannot be unrolled away)
};
[[vk::push_constant]]
ConstantBuffer<CanaryPush> canaryPush;

[[vk::binding(0, 0)]]
Sampler2D canarySource;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    output.position = float4(input.clipPosition, 0.0, 1.0);
    output.uv = input.clipPosition * 0.5 + 0.5;
    return output;
}

[shader("fragment")]
float4 fragmentBw(Varying input) : SV_Target {
    return float4(input.uv * 0.5 + canaryPush.seed, canaryPush.seed, 1.0);
}

[shader("fragment")]
float4 fragmentTex(Varying input) : SV_Target {
    float4 accumulated = float4(0.0, 0.0, 0.0, 0.0);
    float2 p = input.uv;
    for (int i = 0; i < 16; i++) {
        p = frac(p * float2(1.618, 2.718) + float2(0.37 + float(i) * 0.071, 0.61 + canaryPush.seed));
        accumulated += canarySource.SampleLevel(p, 0.0);
    }
    return accumulated * (1.0 / 16.0);
}

[shader("fragment")]
float4 fragmentAlu(Varying input) : SV_Target {
    float x = input.uv.x * 1.3 + canaryPush.seed;
    float y = input.uv.y * 0.7 + 0.1;
    int n = int(canaryPush.count);
    for (int i = 0; i < n; i++) {
        x = frac(mad(x, 1.7, y));
        y = frac(mad(y, 1.3, x));
    }
    return float4(x, y, x * y, 1.0);
}
