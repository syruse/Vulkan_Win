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

layout(location = 0) out vec3 fragTexCoord;

void main() {
    gl_Position = uboModel.MVP * vec4(500*inPosition, 1.0f);
	fragTexCoord = inPosition;
    fragTexCoord.xy *= -1;
}
