#version 450

layout(binding = 2) uniform samplerCube samplerCubeMap;

layout(location = 0) in vec3 fragTexCoord;

layout(location = 0) out vec4 out_Color;

void main() {
  out_Color = texture(samplerCubeMap, fragTexCoord);
}
