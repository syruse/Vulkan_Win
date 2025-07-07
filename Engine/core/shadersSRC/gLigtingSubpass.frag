#version 450

precision highp float;

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputGPassNormal;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputGPassColor;
/** now the depth is separated since SSAO prepass
* layout(input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputDepth;
*/
layout(input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputSSAO;
layout(binding = 3) uniform sampler2D inputDepth;
layout(binding = 4) uniform sampler2D inputShadowMap;


layout(set = 0, binding = 5) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
	mat4 footPrintViewProj;
} uboViewProjection;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
} pushConstant;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_hdr;
layout(location = 2) out vec4 out_shading;

// uncomment if you need draw attachment content
// #define DEBUG_DEPTH 1
// #define DEBUG_SHADOW 1

const float shiness = 8.5;
const float softShadingFactor = 0.45; // soft shading by minimum factor limitation
const float brightness = 2.7;

float getShading(vec3 world, float bias)
{
    vec4 lightPerspective = uboViewProjection.lightViewProj * vec4(world, 1.0);
    vec3 normalizedCoords = lightPerspective.xyz / lightPerspective.w;
    normalizedCoords = (normalizedCoords * 0.5) + vec3(0.5);
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
        float depth = texture(inputDepth, in_uv).r;
        float nearPlane = pushConstant.windowSize.w;
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
        vec4 clip = vec4(xyNormalized * 2.0 - vec2(1.0), texture(inputDepth, in_uv).x, 1.0); // xy : [0;1] -> [-1;1]
    */
    
    float depth = texture(inputDepth, in_uv).r;
    vec4 clip = vec4(in_uv * 2.0 - 1.0, depth, 1.0);
    vec4 world_w = uboViewProjection.viewProjInverse * clip;
    vec3 world = world_w.xyz / world_w.w;
    // Load normal from tile buffer.
    vec3 normalRange_0_1 = subpassLoad(inputGPassNormal).xyz;
	
	out_color = vec4(0.0);
	out_hdr = vec4(0.0);
	out_shading = vec4(1.0);

    // excluding skybox etc with zero length normal
    if (all(greaterThan(normalRange_0_1, vec3(0.0)))) {
		vec3 normal = 2.0 * normalRange_0_1 - 1.0;
		
		float ambientOcclusion = subpassLoad(inputSSAO).r;
		
		// Blinn-Phong lighting model calculation
        vec3 lightDir   = normalize(pushConstant.lightPos - world);
		vec3 viewDir    = normalize(pushConstant.cameraPos - world);
		
		vec3 specInputDir = vec3(0.0);
		// Blinn-Phong
		vec3 reflectDir = reflect(-lightDir, normal);
		specInputDir = reflectDir;
        /* alternative lighting calculation
		  vec3 halfwayDir = normalize(lightDir + viewDir);
		  specInputDir = halfwayDir;
		*/
		
		float spec = pow(max(dot(normal, specInputDir), 0.0), shiness);
		
		// if the surface would have a steep angle to the light source, the shadows may still display shadow acne
		// the bias based on dot product of normal and lightDir will solve this issue
		float bias = max(0.47 * (1.0 - dot(normal, normalize(lightDir))), 0.14);
        float shading = clamp(getShading(world, bias), softShadingFactor, 1.0);
		
		vec4 final_color = vec4(1.0);
		
		final_color.rgb = brightness * (albedo.rgb) + (spec * albedo.rgb);
	    final_color.a = albedo.a;

		out_color = final_color;
		
		// really bright area (which goes beyound ldr color range [0;1]) will be located into hdr render target for bloom effect
		if(dot(final_color.rgb, vec3(0.2126, 0.7152, 0.0722)) > 1.0) // vec3(0.2126, 0.7152, 0.0722) is correct way of translating into gray-scale
			out_hdr = final_color;
		// now shading (shadows + ambientOcclusion) is moved to separate pass where blurring is applied for SSAO
		out_shading = vec4(vec3(shading * ambientOcclusion), 1.0);
	}
#endif
}
