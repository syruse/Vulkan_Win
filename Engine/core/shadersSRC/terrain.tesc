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

// Returns the same tessLevel for two neighboring triangles as long as they are given
// the same pair of edge endpoints, since it never looks at the opposite (non-shared) vertex.
float edgeTessLevel(vec3 edgeA, vec3 edgeB)
{
    float dist = distance(pushConstant.cameraPos.xyz, 0.5 * (edgeA + edgeB));
    return dist < 0.35 * pushConstant.windowSize.z ? pushConstant.cameraPos.w : 1.0;
}

void main()
{
    //Pass along the values to the tessellation evaluation shader.
    outNormal[gl_InvocationID] = inNormal[gl_InvocationID];
    outTexCoord[gl_InvocationID] = inTexCoord[gl_InvocationID];
    outTexCoordNormalized[gl_InvocationID] = inTexCoordNormalized[gl_InvocationID];

    //Calculate tht tessellation levels.
    if (gl_InvocationID == 0)
    {
        vec3 position1 = gl_in[0].gl_Position.xyz;
		vec3 position2 = gl_in[1].gl_Position.xyz;
		vec3 position3 = gl_in[2].gl_Position.xyz;

        // Outer edges are shared with the neighboring triangle across each edge, so they must be
        // derived from that edge alone (not the triangle centroid). Otherwise two triangles that
        // disagree on their own centroid distance generate a different vertex count along the
        // shared edge, leaving a crack (visible sky) between a highly-tessellated tile and its
        // coarser neighbor.
        gl_TessLevelOuter[0] = edgeTessLevel(position2, position3);
        gl_TessLevelOuter[1] = edgeTessLevel(position3, position1);
        gl_TessLevelOuter[2] = edgeTessLevel(position1, position2);
        // Inner level only affects internal subdivision, not shared edges, so the centroid is fine here.
        gl_TessLevelInner[0] = edgeTessLevel(position1, (position2 + position3) * 0.5);
    }
	gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}
