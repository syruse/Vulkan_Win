#version 450

layout(set = 0, binding = 0) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
} dynamicUBO;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
} pushConstant;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragTexCoord;

void main() {
    vec3 vert = 0.5 * pushConstant.windowSize.z * vec3(inPosition.x, 0.15 * inPosition.y + 0.15, inPosition.z); // y coord [-1;1] -> [0; 0.3]
    gl_Position = dynamicUBO.MVP * vec4(vert, 1.0f);
    fragTexCoord = inPosition;
    fragTexCoord.xy *= -1; // inverting since we inside cube
}
