#version 450

layout(set = 0, binding = 1) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
} dynamicUBO;

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
	mat4 footPrintViewProj;
} uboViewProjection;

layout(location = 0) in vec3 inPosition;

void main() {
    vec4 worldPos = dynamicUBO.model * vec4(inPosition, 1.0f);
    gl_Position = uboViewProjection.footPrintViewProj * worldPos;
}
