#version 450

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
} uboViewProjection;

layout(set = 0, binding = 1) uniform UDBOModelObject {
    mat4 model;
	mat4 MVP;
} uboModel;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;

void main() {
	gl_Position = uboModel.MVP * vec4(inPosition, 1.0f);
    outNormal = inNormal * mat3(uboModel.model);
	outTexCoord = inTexCoord;
}