#version 450

// Spec 0007 / Plan 0007 Section 12: this round's one vertex shader --
// position + one per-vertex color attribute, one push-constant
// object-to-world matrix, one uniform-buffer camera (view/projection).
// No lighting, no texture sample, matching Spec 0007's minimal-material
// Non-Goal exactly.

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

layout(push_constant) uniform PushConstants {
  mat4 objectToWorld;
} pushConstants;

layout(binding = 0) uniform CameraUniform {
  mat4 view;
  mat4 projection;
} camera;

layout(location = 0) out vec3 fragColor;

void main() {
  gl_Position = camera.projection * camera.view * pushConstants.objectToWorld * vec4(position, 1.0);
  fragColor = color;
}
