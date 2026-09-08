#version 450

layout(set = 0, binding = 0) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
} dynamicUBO;

layout(set = 0, binding = 2) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
    mat4 footPrintViewProj;
    mat4 prevViewProj;
} uboViewProjection;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent; // last component indicates availability of bump-mapping applying
layout(location = 4) in vec3 inBitangent;

// Instance attributes
layout(location = 5) in vec3 posShift;
layout (location = 6) in float scale;
layout(location = 7) in vec4 model_col0;
layout(location = 8) in vec4 model_col1;
layout(location = 9) in vec4 model_col2;
layout(location = 10) in vec4 model_col3;

layout(location = 0) 
out VS_OUT {
    vec2 TexCoord;
    float isBumpMapping;
    mat3 TBN;
} vs_out;

void main() {
	// 'dynamicUBO.model' is more accurate and can be used for the first instance (gl_InstanceIndex == 0)
    // mat4 instanceModelMat = gl_InstanceIndex == 0 ? dynamicUBO.model : mat4(model_col0, model_col1, model_col2, model_col3);
    // but logic becomes inconsistent and more error prone
    mat4 instanceModelMat = mat4(model_col0, model_col1, model_col2, model_col3);
    // Always go through the universal viewProj (not the per-instance-baked dynamicUBO.MVP): MVP bakes in
    // instance 0's (player) model matrix, so reusing it for other instances (e.g. NPC tanks) would apply
    // the player's rotation/translation on top of their already-correct world-space position.
    vec4 originPos = instanceModelMat * vec4(scale * inPosition, 1.0);
	vec3 pos = originPos.xyz + posShift;
    gl_Position = uboViewProjection.viewProj * vec4(pos, 1.0f);
    vec3 T = vec3(0.0, 0.0, 0.0);
    vec3 B = vec3(0.0, 0.0, 0.0);
    vec3 N = normalize(mat3(instanceModelMat) * inNormal);
    if (inTangent.w > 0.0) {
        T = normalize(mat3(instanceModelMat) * inTangent.xyz);
        B = normalize(mat3(instanceModelMat) * inBitangent);
    }
    vs_out.TBN = mat3(T, B, N);
    vs_out.TexCoord = inTexCoord;
    vs_out.isBumpMapping = inTangent.w;
}
