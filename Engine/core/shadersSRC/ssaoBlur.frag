#version 450

layout(binding = 0) uniform sampler2D inputSSAOTexture;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
    vec4 windDirElapsedTimeMS;
    // x stores the SSAO enable flag.
    uvec4 renderOptions;
} pushConstant;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 out_color;

const int blurFactor = 2;

void main()
{
    // let's keep it commented, that's not releated only SSAO (also shadows) and 
    // that doesn't give us significant benefits
    // SSAO enable flag: 0 means skip SSAO blur but keep applying shadow shading.
    // if (pushConstant.renderOptions.x == 0) {
    //     float shadowShading = texture(inputSSAOTexture, fragTexCoord).r;
    //     out_color = vec4(vec3(0.0), 1.0 - shadowShading);
    //     return;
    // }

    vec2 texelSize = 1.0 / vec2(textureSize(inputSSAOTexture, 0));
    float result = 0.0;
	vec2 offset = vec2(0.0);
    for (int x = -blurFactor; x < blurFactor; ++x) 
    {
        for (int y = -blurFactor; y < blurFactor; ++y) 
        {
            offset = vec2(float(x), float(y)) * texelSize;
            result += texture(inputSSAOTexture, fragTexCoord + offset).r;
        }
    }
	
    result /= pow(blurFactor + blurFactor, 2);

    out_color = vec4(vec3(0.0), 1.0 - result);
	// for debug
	// out_color = vec4(vec3(result), 1.0);
}
