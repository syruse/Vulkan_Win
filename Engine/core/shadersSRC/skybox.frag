#version 450

layout(binding = 1) uniform samplerCube samplerCubeMap;

layout(location = 0) in vec3 fragTexCoord;

layout(location = 0) out vec4 out_Color;
layout(location = 4) out vec4 outReactiveMask;

void main() {
  out_Color = texture(samplerCubeMap, fragTexCoord);
  outReactiveMask = vec4(1.0, 0.0, 0.0, 0.0);
}
