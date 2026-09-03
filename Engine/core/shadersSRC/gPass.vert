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
	// 'dynamicUBO.model' not used, instead we have per instance animation model 'instanceModelMat'
	// 'dynamicUBO.model' is more accurate for and used for the first instance (gl_InstanceIndex == 0)
    mat4 instanceModelMat = gl_InstanceIndex == 0 ? dynamicUBO.model : mat4(model_col0, model_col1, model_col2, model_col3);
    vec3 localPos = scale * inPosition;
    // dynamicUBO.model[3].w is an explicit CPU-set flag (1.0 = MVP already bakes in this model matrix,
    // e.g. tank/barrel; 0.0 = position comes from posShift and rotation must be applied here manually,
    // e.g. instanced cubes/trunk/projectile). Using the flag instead of "is translation zero" avoids
    // misclassifying the tank/barrel when they happen to sit exactly at the world origin.
    // [3].w is chosen because it's always 1.0 in any valid affine matrix and otherwise unused, so
    // overwriting it cannot corrupt the matrix's real translation/rotation/scale.
    if (gl_InstanceIndex != 0 || dynamicUBO.model[3].w < 0.5) {
        localPos = mat3(instanceModelMat) * localPos;
    }
	vec3 pos = localPos + posShift;
    gl_Position = dynamicUBO.MVP * vec4(pos, 1.0f);
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
