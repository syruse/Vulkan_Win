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
    vec3 vert = 500.0 * vec3(inPosition.x, 0.15 * inPosition.y + 0.15, inPosition.z); // y coord [-1;1] -> [0; 0.3]
    gl_Position = uboModel.MVP * vec4(vert, 1.0f);
    fragTexCoord = inPosition;
    fragTexCoord.xy *= -1;
}
