#version 450

layout(binding = 1) uniform sampler2D inputTexture;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 inMotionVector;

layout(location = 0) out vec4 outAccum;
layout(location = 1) out vec4 outRevealage;
layout(location = 2) out vec2 out_motionVectors;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
    vec4 particle; // xyz is wind dir, w is elapsedMS
} pushConstant;

void main() {
    vec4 diffColor = texture(inputTexture, fragTexCoord);
    if (diffColor.a <= 0.01) {
        discard;
    }
    outAccum = vec4(diffColor.rgb * diffColor.a, diffColor.a);
    outRevealage = vec4(diffColor.a);
    out_motionVectors = inMotionVector;
}
