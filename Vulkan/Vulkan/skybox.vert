#version 450

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 view;
    mat4 proj;
} uboViewProjection;

layout(push_constant) uniform PushModel {
    mat4 model;
} pushModel;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragTexCoord;

void main() {
    gl_Position = uboViewProjection.proj * uboViewProjection.view * pushModel.model * vec4(inPosition, 1.0f);
	fragTexCoord = inPosition;
    fragTexCoord.y *= -1;
}
