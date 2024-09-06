#version 450

layout(binding = 2) uniform sampler2D texSampler;

layout(location = 0) in vec2 outTexCoord;

void main() {
  vec3 print = texture(texSampler, outTexCoord).rgb;
  // source where all chanels have the same value or bump texture with active blue chanel only
  gl_FragDepth = print.b;
}