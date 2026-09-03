#version 450

layout(input_attachment_index = 0, binding = 0) uniform subpassInput oitAccum;
layout(input_attachment_index = 1, binding = 1) uniform subpassInput oitRevealage;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 accum = subpassLoad(oitAccum);
    float revealage = clamp(subpassLoad(oitRevealage).r, 0.0, 1.0);
    vec3 transparentColor = accum.rgb / max(accum.a, 1e-4);
    outColor = vec4(transparentColor, 1.0 - revealage);
}