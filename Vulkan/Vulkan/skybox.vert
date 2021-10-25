#version 450

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 view;
    mat4 proj;
} uboViewProjection;

layout(set = 0, binding = 1) uniform UDBOModelObject {
    mat4 model;
} uboModel;

layout(push_constant) uniform PushModel {
    mat4 model;
} pushModel;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragTexCoord;

void main() {
    gl_Position = uboViewProjection.proj * uboViewProjection.view * uboModel.model * vec4(500*inPosition, 1.0f);
	fragTexCoord = inPosition;
    fragTexCoord.xy *= -1;
}
