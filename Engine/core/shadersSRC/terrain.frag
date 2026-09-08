#version 450

// 2D Array of textures: noise, texture1, texture2
layout(binding = 1) uniform sampler2DArray texSampler;
layout(binding = 3) uniform sampler2D footPrintDepth;

layout(location = 0) in vec3 outNormal;
layout(location = 1) in vec2 outTexCoord;
layout(location = 2) in vec2 outTexCoordNormalized;

layout(location = 0) out vec4 out_Color; // not used in g-pass
layout(location = 1) out vec4 out_GPass[2];

//Adjacent samples are taken not 1 texel away, but 3 texels away from the center. This means the blur is wider and the edges are softer.
#define FOOTPRINT_EDGE_SOFTNESS_TEXELS 3.0

// Samples the footprint mask with a soft edge so color fading matches terrain displacement.
float sampleFootPrintAmount(vec2 uv)
{
  vec2 texel = FOOTPRINT_EDGE_SOFTNESS_TEXELS / vec2(textureSize(footPrintDepth, 0));

  float depth = 0.0;
  // 3x3 weighted kernel for soft blurring
  // 0.05  0.10  0.05
  // 0.10  0.40  0.10
  // 0.05  0.10  0.05
  // The sum is 1.0, meaning the brightness/depth remained unchanged on average;
  // the center remained dominant, meaning the footprint wouldn't become too blurry;
  // the neighboring edges softened the edge of the footprint mask;
  // the diagonals has a lesser effect, ensuring the smoothing was rounded but not excessive.
  depth += texture(footPrintDepth, uv).r * 0.40;
  depth += texture(footPrintDepth, uv + vec2(-texel.x,  0.0)).r * 0.10;
  depth += texture(footPrintDepth, uv + vec2( texel.x,  0.0)).r * 0.10;
  depth += texture(footPrintDepth, uv + vec2( 0.0, -texel.y)).r * 0.10;
  depth += texture(footPrintDepth, uv + vec2( 0.0,  texel.y)).r * 0.10;
  depth += texture(footPrintDepth, uv + vec2(-texel.x, -texel.y)).r * 0.05;
  depth += texture(footPrintDepth, uv + vec2( texel.x, -texel.y)).r * 0.05;
  depth += texture(footPrintDepth, uv + vec2(-texel.x,  texel.y)).r * 0.05;
  depth += texture(footPrintDepth, uv + vec2( texel.x,  texel.y)).r * 0.05;

  return clamp(1.0 - depth, 0.0, 1.0);
}

void main() {
  // Trails factor (fading of the result color)
  float trailsFactor = 1.0 - sampleFootPrintAmount(outTexCoordNormalized);
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
