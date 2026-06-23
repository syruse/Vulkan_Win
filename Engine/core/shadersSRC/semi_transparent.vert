#version 450

layout(set = 0, binding = 0) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
    mat4 prevModel;
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

// Instance attributes
layout(location = 5) in vec3 posShift;
layout (location = 6) in float scale;
layout(location = 7) in vec4 model_col0;
layout(location = 8) in vec4 model_col1;
layout(location = 9) in vec4 model_col2;
layout(location = 10) in vec4 model_col3;
layout(location = 11) in vec4 prev_model_col0;
layout(location = 12) in vec4 prev_model_col1;
layout(location = 13) in vec4 prev_model_col2;
layout(location = 14) in vec4 prev_model_col3;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outMotionVector;

void main() {
    // 'dynamicUBO.model' not used, instead we have per instance animation model 'instanceModelMat'
    mat4 instanceModelMat = mat4(model_col0, model_col1, model_col2, model_col3); // it's too resource intensive to have mat4 for each instance, it takes ~5fps
    vec4 origin_pos = instanceModelMat * vec4(scale * inPosition, 1.0);
	vec3 pos = origin_pos.xyz + posShift;
	gl_Position = dynamicUBO.MVP * vec4(pos, 1.0f);
	outNormal = normalize(mat3(instanceModelMat) * inNormal);
	outTexCoord = inTexCoord;

    mat4 prevInstanceModelMat = gl_InstanceIndex == 0 ? dynamicUBO.prevModel : mat4(prev_model_col0, prev_model_col1, prev_model_col2, prev_model_col3);
    vec4 prevOriginPos = prevInstanceModelMat * vec4(scale * inPosition, 1.0);
    vec3 prevPos = prevOriginPos.xyz + posShift;
    vec4 prevClip = uboViewProjection.prevViewProj * vec4(prevPos, 1.0);

    vec2 currentNDCPos = gl_Position.xy / gl_Position.w;
    vec2 prevNDCPos = prevClip.xy / prevClip.w;
    outMotionVector = currentNDCPos - prevNDCPos;
}