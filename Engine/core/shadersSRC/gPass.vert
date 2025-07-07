#version 450

layout(set = 0, binding = 0) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
} dynamicUBO;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent; // last component indicates availability of bump-mapping applying
layout(location = 4) in vec3 inBitangent;

// Instance attributes
layout(location = 5) in vec3 posShift;
layout (location = 6) in float scale;

layout(location = 0) 
out VS_OUT {
    vec2 TexCoord;
    float isBumpMapping;
    mat3 TBN;
} vs_out;

void main() {
	vec3 pos = inPosition + posShift;
    gl_Position = dynamicUBO.MVP * vec4(scale*(pos), 1.0f);
    vec3 T = vec3(0.0, 0.0, 0.0);
    vec3 B = vec3(0.0, 0.0, 0.0);
    vec3 N = normalize(mat3(dynamicUBO.model) * inNormal);
    if (inTangent.w > 0.0) {
        T = normalize(mat3(dynamicUBO.model) * inTangent.xyz);
        B = normalize(mat3(dynamicUBO.model) * inBitangent);
    }
    vs_out.TBN = mat3(T, B, N);
    vs_out.TexCoord = inTexCoord;
    vs_out.isBumpMapping = inTangent.w;
}
