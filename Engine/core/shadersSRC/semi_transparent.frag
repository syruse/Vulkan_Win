#version 450

layout(binding = 1) uniform sampler2D inputTexture;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 inMotionVector;

layout(location = 0) out vec4 out_Color;
layout(location = 1) out vec2 out_motionVectors;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
    vec4 particle; // xyz is wind dir, w is elapsedMS
} pushConstant;

void main() {
    vec4 diffColor = texture(inputTexture, fragTexCoord);
    // if gradient enabled then we multiply the color by gradient color
    out_Color = diffColor;
    out_motionVectors = inMotionVector;
}
