#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent; // XY are stretched UV for oversampling\repetition effect of tile pattern
layout(location = 4) in vec3 inBitangent; // currently not used

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec2 outTexCoordNormalized;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec4 cameraPos; // the last component is maxTessellationGenerationLevel
} pushConstant;

void main() {
    gl_Position = vec4(inPosition, 1.0f);
    outNormal = inNormal;
    outTexCoord = inTangent.xy;
    outTexCoordNormalized = inTexCoord;
}
