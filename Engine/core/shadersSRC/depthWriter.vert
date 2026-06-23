#version 450

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
	mat4 footPrintViewProj;
    mat4 prevViewProj;
} uboViewProjection;

layout(set = 0, binding = 1) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
    mat4 prevModel;
} dynamicUBO;

layout(location = 0) out vec4 outPosInViewSpace;
layout(location = 1) out vec2 outMotionVector;

layout(location = 0) in vec3 inPosition;

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

void main() {
    // 'dynamicUBO.model' not used, instead we have per instance animation model 'instanceModelMat'
    // 'dynamicUBO.model' is more accurate for and used for the first instance (gl_InstanceIndex == 0)
    mat4 instanceModelMat = gl_InstanceIndex == 0 ? dynamicUBO.model : mat4(model_col0, model_col1, model_col2, model_col3); // it's too resource intensive to have mat4 for each instance, it takes ~5fps
    vec4 originPos = instanceModelMat * vec4(scale * inPosition, 1.0);
	vec3 pos = originPos.xyz + posShift;
	outPosInViewSpace = uboViewProjection.view * vec4(pos, 1.0);
    gl_Position = uboViewProjection.viewProj * vec4(pos, 1.0);

    // calculate motion vector using previous frame's model matrix and current frame's model matrix
    mat4 prevInstanceModelMat = gl_InstanceIndex == 0 ? dynamicUBO.prevModel : mat4(prev_model_col0, prev_model_col1, prev_model_col2, prev_model_col3);
    vec4 originPrevPos = prevInstanceModelMat * vec4(scale * inPosition, 1.0);
    vec3 prevPos = originPrevPos.xyz + posShift;
    vec4 prevPosInClipSpace = uboViewProjection.prevViewProj * vec4(prevPos, 1.0);
    vec2 currentNDCPos = gl_Position.xy / gl_Position.w;
    vec2 prevNDCPos = prevPosInClipSpace.xy / prevPosInClipSpace.w;
    outMotionVector = currentNDCPos - prevNDCPos;
}
