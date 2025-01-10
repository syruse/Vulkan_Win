#version 450

layout(set = 0, binding = 0) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
} dynamicUBO;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out float fragDepth;

void main() {
    gl_Position = dynamicUBO.MVP * vec4(inPosition, 1.0f);
    outNormal = normalize(mat3(dynamicUBO.model) * inNormal);
    outTexCoord = inTexCoord;
	
	vec4 clip = gl_Position;
    float ndc_z = clip.z / clip.w; // no need to execute win_z = ndc_z * 0.5 + 0.5 since we force glm to produce z range [0; 1] 
    fragDepth = ndc_z;
}