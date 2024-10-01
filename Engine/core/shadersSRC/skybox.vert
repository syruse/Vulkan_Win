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
	vec3 vert = vec3(inPosition.x, 0.3 * inPosition.y + 0.15, inPosition.z); // y coord [-1;1] -> [-0.15; 0.45] to hide mountains in skybox textures
    gl_Position = dynamicUBO.MVP * vec4(vert, 1.0f);
	gl_Position.z = gl_Position.w; // this is the trick to get z component being always equal to 1.0, because when the perspective division is applied its z component translates to w / w = 1.0
    fragTexCoord = inPosition;
    fragTexCoord.xy *= -1; // inverting since we inside cube
}
