#version 450

layout (vertices = 3) out;
 
layout(location = 0) in vec3 inNormal[];
layout(location = 1) in vec2 inTexCoord[];
layout(location = 2) in vec2 inTexCoordNormalized[];
 
layout (location = 0) out vec3 outNormal[];
layout (location = 1) out vec2 outTexCoord[];
layout (location = 2) out vec2 outTexCoordNormalized[];

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec4 cameraPos; // the last component is maxTessellationGenerationLevel
} pushConstant;

void main()
{
    //Pass along the values to the tessellation evaluation shader.
    outNormal[gl_InvocationID] = inNormal[gl_InvocationID];
    outTexCoord[gl_InvocationID] = inTexCoord[gl_InvocationID];
    outTexCoordNormalized[gl_InvocationID] = inTexCoordNormalized[gl_InvocationID];

    //Calculate tht tessellation levels.
    if (gl_InvocationID == 0)
    {
	    float tessLevel = 1; // no tesseletation for far tiles
		
        vec3 position1 = gl_in[0].gl_Position.xyz;
		vec3 position2 = gl_in[1].gl_Position.xyz;
		vec3 position3 = gl_in[2].gl_Position.xyz;
		vec3 center = (position1 + position2 + position3) / 3.0;
        float distance = distance(pushConstant.cameraPos.xyz, center);
		if (distance < 0.35 * pushConstant.windowSize.z) // 25 percentage of far plane
		{
			tessLevel = pushConstant.cameraPos.w;
		}

        gl_TessLevelInner[0] = tessLevel;
        gl_TessLevelOuter[0] = tessLevel;
        gl_TessLevelOuter[1] = tessLevel;
        gl_TessLevelOuter[2] = tessLevel;
    }
	gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}
