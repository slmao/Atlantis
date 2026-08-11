#version 450

// Spec 0007 / Plan 0007 Section 12: outputs the interpolated per-vertex
// color unmodified -- no lighting, no texture sample.

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
  outColor = vec4(fragColor, 1.0);
}
