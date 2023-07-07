#version 450

layout(set = 0, binding = 0) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
} dynamicUBO;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec2 outTexCoordNormalized;

void main() {
    gl_Position = dynamicUBO.MVP * vec4(inPosition, 1.0f);
    outNormal = inNormal * mat3(dynamicUBO.model);
    outTexCoord = inTexCoord;
    outTexCoordNormalized = clamp(inTexCoord, vec2(0.0, 0.0), vec2(1.0, 1.0));
}
