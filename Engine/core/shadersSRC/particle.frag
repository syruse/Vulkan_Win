#version 450

layout(binding = 1) uniform sampler2D inputTexture;
layout(binding = 2) uniform sampler2D inputGradient;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in float fragDepth;
layout(location = 2) in float kFading;
layout(location = 3) flat in int isGradientEnabled;
layout(location = 4) in float alpha;
layout(location = 5) in vec2 inMotionVector;

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
    // if gradient enabled then we multiply the color by gradient color
    vec4 particleColor =
        mix(diffColor, diffColor * vec4(texture(inputGradient, fragTexCoord).rgb, alpha * (1.0 - kFading)), isGradientEnabled);
    if (particleColor.a <= 0.01) {
        discard;
    }
    // McGuire weighted-blended OIT weight: particles are the only remaining OIT accum/revealage
    // consumer now (foliage draws directly with depth write, see semi_transparent.frag), so this
    // only needs to correctly order overlapping smoke/particle layers against each other.
    float weight = clamp(pow(min(1.0, particleColor.a * 10.0) + 0.01, 3.0) * 1e8 * pow(1.0 - gl_FragCoord.z * 0.9, 3.0), 1e-2, 3e3);
    outAccum = vec4(particleColor.rgb * particleColor.a, particleColor.a) * weight;
    outRevealage = vec4(particleColor.a);
    out_motionVectors = inMotionVector;
    gl_FragDepth = fragDepth;
}
