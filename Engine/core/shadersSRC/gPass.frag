#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 outNormal;
layout(location = 1) in vec2 outTexCoord;

layout(location = 0) out vec4 out_Color; // not used in g-pass
layout(location = 1) out vec4 out_GPass[2];

void main() {
  // Normals, pack -1, +1 range to 0, 1.
  out_GPass[0] = vec4(0.5 * normalize(outNormal) + 0.5, 1.0);
  out_GPass[1] = texture(texSampler, outTexCoord);
}
