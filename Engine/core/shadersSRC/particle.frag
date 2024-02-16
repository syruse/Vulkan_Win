#version 450

layout(binding = 1) uniform sampler2D inputTexture;
layout(binding = 2) uniform sampler2D inputDepth;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 out_Color;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
} pushConstant;

float linearizeDepth(float depth, float nearPlane, float farPlane) {
    float linearDepth = (2.0 * nearPlane) / (farPlane + nearPlane - depth * (farPlane - nearPlane));
    return linearDepth;
}

void main() {
  float nearPlane = 1.0;
  float farPlane = pushConstant.windowSize.z;
  vec2 texCoord = vec2(gl_FragCoord.x / pushConstant.windowSize.x, gl_FragCoord.y / pushConstant.windowSize.y);
  float cachedDepth = texture(inputDepth, texCoord).r;
  // applying depth test by itself since we have gpass and draw semi-transparent objects at the end 
  // taking into accound depth texture of all previously rendered objects
  if (linearizeDepth(gl_FragCoord.z, nearPlane, farPlane) > linearizeDepth(cachedDepth, nearPlane, farPlane))
  {
      discard;
  }
  else
  {
      out_Color = texture(inputTexture, fragTexCoord);
  }
}
