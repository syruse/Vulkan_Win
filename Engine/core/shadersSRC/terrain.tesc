#version 450

layout (vertices = 3) out;
 
layout(location = 0) in vec3 inNormal[];
layout(location = 1) in vec2 inTexCoord[];
layout(location = 2) in vec2 inTexCoordNormalized[];
 
layout (location = 0) out vec3 outNormal[];
layout (location = 1) out vec2 outTexCoord[];
layout (location = 2) out vec2 outTexCoordNormalized[];

void main()
{
    //Pass along the values to the tessellation evaluation shader.
    outNormal[gl_InvocationID] = inNormal[gl_InvocationID];
    outTexCoord[gl_InvocationID] = inTexCoord[gl_InvocationID];
    outTexCoordNormalized[gl_InvocationID] = inTexCoordNormalized[gl_InvocationID];

    //Calculate tht tessellation levels.
    if (gl_InvocationID == 0)
    {
        //vec4 position1 = gl_in[0].gl_Position;
 
        gl_TessLevelInner[0] = 16;
        gl_TessLevelOuter[0] = 16;
        gl_TessLevelOuter[1] = 16;
        gl_TessLevelOuter[2] = 16;
    }
	gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}