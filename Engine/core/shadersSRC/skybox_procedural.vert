#version 450
layout(set = 0, binding = 0) uniform DynamicUBO { mat4 model; mat4 MVP; } dynamicUBO;
layout(location = 0) in vec3 inPosition;
layout(location = 1) out vec3 vDir;

void main() {
    vDir = inPosition; // Pure direction vector
    vec3 vert = vec3(inPosition.x, 0.3 * inPosition.y + 0.15, inPosition.z); // y coord [-1;1] -> [-0.15; 0.45] to hide mountains in skybox textures
    gl_Position = dynamicUBO.MVP * vec4(vert, 1.0);
    gl_Position.z = gl_Position.w;
}