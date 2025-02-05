#version 450

layout(binding = 1) uniform sampler2D inputTexture;
layout(binding = 2) uniform sampler2D inputGradient;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in float fragDepth;
layout(location = 2) in float kFading;
layout(location = 3) flat in int isGradientEnabled;
layout(location = 4) in float alpha;

layout(location = 0) out vec4 out_Color;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
    vec4 particle; // xyz is wind dir, w is elapsedMS
} pushConstant;

void main() {
    vec4 diffColor = texture(inputTexture, fragTexCoord);
    // if gradient enabled then we multiply the color by gradient color
    out_Color = mix(diffColor, diffColor * vec4(texture(inputGradient, fragTexCoord).rgb, alpha*(1.0 - kFading)), isGradientEnabled);
	gl_FragDepth = fragDepth;
}
