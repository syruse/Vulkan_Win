#version 450

layout(binding = 0) uniform sampler2D inputTexture;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 out_color;

void main()
{
    float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 texel_size = 1.0 / textureSize(inputTexture, 0);
    vec4 result = texture(inputTexture, fragTexCoord) * weight[0]; 
    for(int i = 1; i < 5; ++i)
    {
        result += texture(inputTexture, fragTexCoord + vec2(0.0, texel_size.y * i)) * weight[i];
        result += texture(inputTexture, fragTexCoord - vec2(0.0, texel_size.y * i)) * weight[i];
    }

    out_color = result;
}
