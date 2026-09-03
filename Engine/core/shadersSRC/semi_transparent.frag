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
    // McGuire's weighted-blended OIT weight: this was missing entirely before, so accum/revealage
    // summed every overlapping fragment (leaves and smoke) with equal weight regardless of depth,
    // which is what caused both the hazy foliage blur and the original smoke/leaves order bug.
    // Nearer fragments get a much higher weight, so they dominate the resolve pass's weighted average.
    float weight = clamp(pow(min(1.0, diffColor.a * 10.0) + 0.01, 3.0) * 1e8 * pow(1.0 - gl_FragCoord.z * 0.9, 3.0), 1e-2, 3e3);
    outAccum = vec4(diffColor.rgb * diffColor.a, diffColor.a) * weight;
    outRevealage = vec4(diffColor.a);
    out_motionVectors = inMotionVector;
}
