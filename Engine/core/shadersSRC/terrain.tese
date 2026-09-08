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
// Adjacent samples are taken not 1 texel away, but 3 texels away from the center. This means the blur is wider and the edges are softer.
#define FOOTPRINT_EDGE_SOFTNESS_TEXELS 3.0

// Samples the footprint mask with a soft edge so terrain displacement matches color fading.
float sampleFootPrintAmount(vec2 uv)
{
    vec2 texel = FOOTPRINT_EDGE_SOFTNESS_TEXELS / vec2(textureSize(footPrintDepth, 0));

    float depth = 0.0;
    // 3x3 weighted kernel for soft blurring
    // 0.05  0.10  0.05
    // 0.10  0.40  0.10
    // 0.05  0.10  0.05
    // The sum is 1.0, meaning the brightness/depth remained unchanged on average;
    // the center remained dominant, meaning the footprint wouldn't become too blurry;
    // the neighboring edges softened the edge of the footprint mask;
    // the diagonals has a lesser effect, ensuring the smoothing was rounded but not excessive.
    depth += texture(footPrintDepth, uv).r * 0.40;
    depth += texture(footPrintDepth, uv + vec2(-texel.x,  0.0)).r * 0.10;
    depth += texture(footPrintDepth, uv + vec2( texel.x,  0.0)).r * 0.10;
    depth += texture(footPrintDepth, uv + vec2( 0.0, -texel.y)).r * 0.10;
    depth += texture(footPrintDepth, uv + vec2( 0.0,  texel.y)).r * 0.10;
    depth += texture(footPrintDepth, uv + vec2(-texel.x, -texel.y)).r * 0.05;
    depth += texture(footPrintDepth, uv + vec2( texel.x, -texel.y)).r * 0.05;
    depth += texture(footPrintDepth, uv + vec2(-texel.x,  texel.y)).r * 0.05;
    depth += texture(footPrintDepth, uv + vec2( texel.x,  texel.y)).r * 0.05;

    return clamp(1.0 - depth, 0.0, 1.0);
}
 
void main()
{
    // Pass the values along to the fragment shader.
    // For triangles, the vertex’s position is a barycentric coordinate (builtin TessCoord with u,v,w), where u + v + w = 1.0, and indicates the relative influence of the three vertices of the triangle on the position of the vertex
    outNormal = gl_TessCoord.x * inNormal[0] + gl_TessCoord.y * inNormal[1] + gl_TessCoord.z * inNormal[2];
    outTexCoord = gl_TessCoord.x * inTexCoord[0] + gl_TessCoord.y * inTexCoord[1] + gl_TessCoord.z * inTexCoord[2];
	outTexCoordNormalized = gl_TessCoord.x * inTexCoordNormalized[0] + gl_TessCoord.y * inTexCoordNormalized[1] + gl_TessCoord.z * inTexCoordNormalized[2];
 
    vec4 position = gl_TessCoord.x * gl_in[0].gl_Position + gl_TessCoord.y * gl_in[1].gl_Position + gl_TessCoord.z * gl_in[2].gl_Position;
	
    float footPrintFactor = sampleFootPrintAmount(outTexCoordNormalized);
	position.y += FOOTPRINT_DISPLACEMENT * footPrintFactor;
	
    gl_Position = uboViewProjection.viewProj * position;
}
