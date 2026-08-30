#version 450

// 2D Array of textures: noise, texture1, texture2
layout(binding = 1) uniform sampler2DArray texSampler;
layout(binding = 3) uniform sampler2D footPrintDepth;

layout(location = 0) in vec3 outNormal;
layout(location = 1) in vec2 outTexCoord;
layout(location = 2) in vec2 outTexCoordNormalized;

layout(location = 0) out vec4 out_Color; // not used in g-pass
layout(location = 1) out vec4 out_GPass[2];

void main() {
  // Trails factor (fading of the result color)
  float trailsFactor = texture(footPrintDepth, outTexCoordNormalized).r;
  // Normals, pack -1, +1 range to 0, 1.
  out_GPass[0] = vec4(0.5 * normalize(outNormal) + 0.5, 1.0);
  
  // avoiding repetition effect for texture oversampling
  float noiseFactor = texture(texSampler, vec3(outTexCoordNormalized, 0.0)).x;
  vec4 color1OverSampled = texture(texSampler, vec3(0.7 * outTexCoord, 1.0));
  vec4 color1 = 0.5 * texture(texSampler, vec3(outTexCoordNormalized, 1.0));
  color1 = mix(color1OverSampled, color1, 0.25);
  vec4 color2OverSampled = 0.5 * texture(texSampler, vec3(1.2 * outTexCoord, 2.0));
  vec4 color2 = texture(texSampler, vec3(outTexCoordNormalized, 2.0));
  color2 = mix(color2OverSampled, color2, 0.35);
  out_GPass[1] = mix(color1, color2, noiseFactor);
  out_GPass[1].rgb = trailsFactor * out_GPass[1].rgb; // fading of the result color where there are trails
  out_GPass[1].a = 1.0;
}
