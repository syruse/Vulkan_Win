#version 450

layout(set = 0, binding = 1) uniform DynamicUBO {
    mat4 model;
    mat4 MVP;
} dynamicUBO;

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
	mat4 footPrintViewProj;
} uboViewProjection;

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inTexCoord;

// Instance attributes
layout(location = 5) in vec3 posShift;
layout (location = 6) in float scale;
layout(location = 7) in vec4 model_col0;
layout(location = 8) in vec4 model_col1;
layout(location = 9) in vec4 model_col2;
layout(location = 10) in vec4 model_col3;

layout(location = 0) out vec2 outTexCoord;

void main() {
    // 'dynamicUBO.model' not used, instead we have per instance animation model 'instanceModelMat'
    // 'dynamicUBO.model' is more accurate for and used for the first instance (gl_InstanceIndex == 0)
    mat4 instanceModelMat = gl_InstanceIndex == 0 ? dynamicUBO.model : mat4(model_col0, model_col1, model_col2, model_col3); // it's too resource intensive to have mat4 for each instance, it takes ~5fps
    vec4 origin_pos = instanceModelMat * vec4(scale * inPosition, 1.0);
	vec3 pos = origin_pos.xyz + posShift;
    outTexCoord = inTexCoord;
    gl_Position = uboViewProjection.footPrintViewProj * vec4(pos, 1.0);
}
