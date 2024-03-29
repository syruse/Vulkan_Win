#version 450

// Array for triangle that fills screen
vec2 positions[6] = vec2[](
    vec2(1.0, -1.0),
    vec2(-1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0),
    vec2(1.0, -1.0)
);

layout(location = 0) out vec2 fragTexCoord;

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragTexCoord = clamp(gl_Position.xy, vec2(0.0, 0.0), vec2(1.0, 1.0));
}