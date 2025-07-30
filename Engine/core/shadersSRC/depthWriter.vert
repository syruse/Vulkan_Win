#version 450

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
	mat4 footPrintViewProj;
} uboViewProjection;

layout(set = 0, binding = 1) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
} dynamicUBO;

layout(location = 0) out vec4 outPosInViewSpace;

layout(location = 0) in vec3 inPosition;

// Instance attributes
layout(location = 5) in vec3 posShift;
layout (location = 6) in float scale;

void main() {
	vec3 pos = inPosition + posShift;
	vec4 worldSpacePos = dynamicUBO.model * vec4(scale*(pos), 1.0f);
	outPosInViewSpace = uboViewProjection.view * worldSpacePos;
    gl_Position = uboViewProjection.viewProj * worldSpacePos;
}
