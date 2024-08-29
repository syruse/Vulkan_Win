#version 450

layout (triangles, equal_spacing, cw) in;

layout(binding = 3) uniform sampler2D footPrintDepth;
 
layout (location = 0) in vec3 inNormal[];
layout (location = 1) in vec2 inTexCoord[];
layout (location = 2) in vec2 inTexCoordNormalized[];

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec2 outTexCoordNormalized;

layout(set = 0, binding = 2) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
	mat4 footPrintViewProj;
} uboViewProjection;

// bump the terrain geometry by vehicle wheels
#define FOOTPRINT_DISPLACEMENT -3
 
void main()
{
    // Pass the values along to the fragment shader.
    // For triangles, the vertex’s position is a barycentric coordinate (u,v,w), where u + v + w = 1.0, and indicates the relative influence of the three vertices of the triangle on the position of the vertex
    outNormal = gl_TessCoord.x * inNormal[0] + gl_TessCoord.y * inNormal[1] + gl_TessCoord.z * inNormal[2];
    outTexCoord = gl_TessCoord.x * inTexCoord[0] + gl_TessCoord.y * inTexCoord[1] + gl_TessCoord.z * inTexCoord[2];
	outTexCoordNormalized = gl_TessCoord.x * inTexCoordNormalized[0] + gl_TessCoord.y * inTexCoordNormalized[1] + gl_TessCoord.z * inTexCoordNormalized[2];
 
    vec4 position = gl_TessCoord.x * gl_in[0].gl_Position + gl_TessCoord.y * gl_in[1].gl_Position + gl_TessCoord.z * gl_in[2].gl_Position;
	
	float footPrintFactor = 1.0 - texture(footPrintDepth, outTexCoordNormalized).r;
	position.y += FOOTPRINT_DISPLACEMENT * footPrintFactor;
	
	gl_Position = uboViewProjection.viewProj * position;
}
