#version 450

precision highp float;

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputGPassNormal;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputGPassColor;
layout(input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputDepth;
layout(binding = 3) uniform sampler2D inputShadowMap;

layout(set = 0, binding = 4) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
} uboViewProjection;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
} pushConstant;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

// uncomment if you need draw attachment content
// #define DEBUG_DEPTH 1
// #define DEBUG_SHADOW 1

const bool is_blinnPhong = true;
const float shiness = 8.5;
const float softShadingFactor = 0.35; // soft shading by minimum factor limitation

float getShading(vec3 world, float bias)
{
    vec4 lightPerspective = uboViewProjection.lightViewProj * vec4(world, 1.0);
    vec3 normalizedCoords = lightPerspective.xyz / lightPerspective.w;
    normalizedCoords = normalizedCoords * 0.5 + 0.5;
    float currentDepth = normalizedCoords.z;
    
    // clipping coords which don't fit in normalized range to avoid shading far pixels
    if (currentDepth > 1.0)
    {
        return 1.0;
    }
    
    // calculate average shading basing on nearest pixels
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(inputShadowMap, 0);
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(inputShadowMap, normalizedCoords.xy + vec2(x, y) * texelSize).r;
            // check whether current frag pos is in shadow
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;

    return 1.0 - shadow;
} 

void main()
{
#ifdef DEBUG_DEPTH
    const float widthHalf = pushConstant.windowSize.x / 2.0;
    if(gl_FragCoord.x > widthHalf)
    {
        float depth = subpassLoad(inputDepth).r;
        float nearPlane = 1.0;
        float farPlane = pushConstant.windowSize.z;
        float linearDepth = (2.0 * nearPlane) / (farPlane + nearPlane - depth * (farPlane - nearPlane));
        out_color = vec4(vec3(linearDepth), 1.0f);
    }
    else
    {
        out_color = subpassLoad(inputGPassColor).rgba;
    }
#elif DEBUG_SHADOW
    const float widthHalf = pushConstant.windowSize.x / 2.0;
    if(gl_FragCoord.x > widthHalf)
    {
        out_color = vec4(vec3(texture(inputShadowMap, in_uv).r), 1.0);
    }
    else
    {
        out_color = subpassLoad(inputGPassColor).rgba;
    }
#else
    // Load G-Buffer diffuse color from tile buffer.
    vec4 albedo = subpassLoad(inputGPassColor);
    
    // Load depth from tile buffer and reconstruct world position.
    
    /** not optimized clip space forming
        vec2 xyNormalized = vec2(gl_FragCoord.x / pushConstant.windowSize.x, gl_FragCoord.y / pushConstant.windowSize.y); // getting xy in range [0;1]
        vec4 clip = vec4(xyNormalized * 2.0 - vec2(1.0), subpassLoad(inputDepth).x, 1.0); // xy : [0;1] -> [-1;1]
    */
    
    vec4 clip = vec4(in_uv * 2.0 - 1.0, subpassLoad(inputDepth).x, 1.0);
    vec4 world_w = uboViewProjection.viewProjInverse * clip;
    vec3 world = world_w.xyz / world_w.w;
    // Load normal from tile buffer.
    vec3 normalRange_0_1 = subpassLoad(inputGPassNormal).xyz;
    vec3 normal = 2.0 * normalRange_0_1 - 1.0;
    
    // Blinn-Phong lighting model calculation
    vec3 lightDir   = normalize(pushConstant.lightPos - world);
    vec3 viewDir    = normalize(pushConstant.cameraPos - world);
    
    vec3 specInputDir = vec3(0.0);
    if (is_blinnPhong) {
        vec3 reflectDir = reflect(-lightDir, normal);
        specInputDir = reflectDir;
    }
    else
    {
        vec3 halfwayDir = normalize(lightDir + viewDir);
        specInputDir = halfwayDir;
    }
    float spec = pow(max(dot(normal, specInputDir), 0.0), shiness);
    
    // if the surface would have a steep angle to the light source, the shadows may still display shadow acne
    // the bias based on dot product of normal and lightDir will solve this issue
    float bias = max(0.91 * (1.0 - dot(normal, normalize(lightDir + viewDir))), 0.2);
    float shading = max(getShading(world, bias), softShadingFactor);
    
    vec3 res_color = (shading * albedo.rgb) + (spec * albedo.rgb);

    // length(normalRange_0_1) designates whether it's background pixel or pixel of 3d model
    // preserving existing color (for example skybox color) if it's not g-pass stuff by paiting with transparent color
    out_color = mix(vec4(0.0), vec4(res_color, albedo.a), length(normalRange_0_1));
#endif
}
