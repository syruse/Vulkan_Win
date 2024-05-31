#version 450

precision highp float;

layout (constant_id = 0) const int SSAO_KERNEL_SIZE = 64;

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputGPassNormal;
layout(binding = 1) uniform sampler2D inputDepth;
layout(binding = 2) uniform sampler2D inputNoise;

layout(set = 0, binding = 3) uniform UBOSemisphereKernelObject {
    vec4 samples[SSAO_KERNEL_SIZE];
} uboKernel;

layout(set = 0, binding = 4) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
} uboViewProjection;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

const float noiseScale = 256.0; // oversampling multiplier apllied for 4x4 noise texture
const float radius = 0.41; // semi-sphera kernel radius
const float bias = 0.99;

void main()
{
    vec3 normalRange_0_1 = subpassLoad(inputGPassNormal).xyz;
    // skybox etc has zero length normal
    if (all(greaterThan(normalRange_0_1, vec3(0.0)))) {
        vec3 normal = 2.0 * normalRange_0_1 - 1.0;
        
        vec4 clip = vec4(in_uv * 2.0 - 1.0, texture(inputDepth, in_uv).x, 1.0);
        vec4 world_w = uboViewProjection.viewProjInverse * clip;
        vec4 world = world_w / world_w.w;
        vec4 posInViewSpace = uboViewProjection.view * world;
        
        vec3 randomVec = texture(inputNoise, in_uv * noiseScale).xyz * 2.0 - 1.0;
        
        // The Gram–Schmidt process for generation of tilted orthogonal basis (we don't need accurancy like precalculated tangent)
        // original : vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal)); but the next formula produces much better result
		vec3 tangent = normalize(randomVec - dot(randomVec, normal));
        vec3 bitangent = cross(normal, tangent);
        mat3 TBN = mat3(tangent, bitangent, normal);
        
        float occlusion = 0.0;
        for(int i = 0; i < SSAO_KERNEL_SIZE; ++i)
        {
            vec3 sample_ = TBN * uboKernel.samples[i].xyz;
            sample_ = posInViewSpace.xyz + sample_ * radius; 
            vec4 offset = vec4(sample_, 1.0);
            offset = uboViewProjection.proj * offset;
            offset.xyz /= offset.w;
            clip = vec4(offset.xy, texture(inputDepth, offset.xy * 0.5 + 0.5).r, 1.0);
            world_w = uboViewProjection.viewProjInverse * clip;
            world = world_w / world_w.w;
            vec4 sampleView = uboViewProjection.view * world;
            float rangeCheck = smoothstep(0.0f, 1.0f, radius / abs(posInViewSpace.z - sampleView.z));
            occlusion += (sampleView.z >= sample_.z + bias ? 1.0 : 0.0) * rangeCheck;
        }
        
        occlusion = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
        out_color = vec4(occlusion, 0.0, 0.0, 1.0); // if you wanna more contrast: occlusion * occlusion
    } else {
        out_color = vec4(0.0);
    }
}
