#version 450

layout(set = 0, binding = 0) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
} dynamicUBO;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;

void main() {
    gl_Position = dynamicUBO.MVP * vec4(inPosition, 1.0f);
    outNormal = normalize(mat3(dynamicUBO.model) * inNormal);
    outTexCoord = inTexCoord;
}