#version 450

layout(binding = 0) uniform sampler2D inputTexture;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 out_color;

const float exposure = 4.5;
const float gamma = 2.2;
void main()
{
    vec4 result = texture(inputTexture, fragTexCoord); 

    // exposure tone mapping
    result.xyz = vec3(1.0) - exp(-(result.xyz) * exposure);
    // gamma correction
    result.xyz = pow(result.xyz, vec3(1.0 / gamma));
    out_color = result;
}
