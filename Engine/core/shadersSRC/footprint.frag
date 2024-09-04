#version 450

layout(binding = 2) uniform sampler2D texSampler;

layout(location = 0) in vec2 outTexCoord;

void main() {
  // let's write it ourselves for convinience, it does not incur costs since we have several primitives only
  gl_FragDepth = texture(texSampler, outTexCoord).b;
}