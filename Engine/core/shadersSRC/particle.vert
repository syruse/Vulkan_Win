#version 450

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
} uboViewProjection;

// Instance attributes
layout (location = 0) in vec3 inPos;
layout (location = 1) in float scale;

// Array for triangle that represents the quad
vec2 positions[4] = vec2[](
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(-1.0, 0.0),
    vec2(-1.0, 1.0)
);

layout(location = 0) out vec2 fragTexCoord;

void main()
{
    vec2 node = positions[gl_VertexIndex];
    fragTexCoord = clamp(node, vec2(0.0, 0.0), vec2(1.0, 1.0));
    vec4 fragPos = vec4((inPos + scale * vec3(node, 0.0)), 1.0f);
    gl_Position = uboViewProjection.viewProj * fragPos;
}
