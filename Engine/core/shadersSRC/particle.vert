#version 450

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
} uboViewProjection;

layout(set = 0, binding = 4) uniform UBOParticleObject {
    vec4 dynamicPos;
    vec4 velocity;
    int mode; // '0' regular particles effect(parallel to user face) and '1' spreading along z-plane(which perpendicular to z-plane)
} uboParticle;

// Instance attributes

layout (location = 0) in vec3 inPosOrigin;
layout (location = 1) in vec3 scaleMin;
layout (location = 2) in vec3 scaleMax;

layout (location = 3) in vec3 inPos;
layout (location = 4) in vec3 acceleration;
layout (location = 5) in float lifeDuration;
layout (location = 6) in float speedK;
layout (location = 7) in float alphaK;

// Array for triangle that represents the quad
vec2 quadPos[4] = vec2[](
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(-1.0, 0.0),
    vec2(-1.0, 1.0)
);

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
    vec4 windDirElapsedTimeMS; // xyz is wind dir, w is elapsedMS
} pushConstant;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out float fragDepth;
layout(location = 2) out float kFading;
layout(location = 3) flat out int isGradientEnabled;
layout(location = 4) out float alpha;

void main()
{
    vec3 posOrigin = inPos;
    vec3 scale = scaleMax;
    if (uboParticle.mode == 0) {
        kFading = fract((speedK * pushConstant.windDirElapsedTimeMS.w) / lifeDuration); // [0.0 - 1.0]
        float time = 5 * kFading;
        posOrigin = uboParticle.dynamicPos.xyz + inPosOrigin + time * uboParticle.velocity.xyz + time *  time * acceleration;
        scale = alphaK * mix(scaleMin, scaleMax, kFading);
        alpha = alphaK;
        isGradientEnabled = 1;
    } else {
        posOrigin = inPos;
        scale = scaleMax;
        kFading = 0;
        isGradientEnabled = 0;
    }
    vec4 cameraSpace_pos = uboViewProjection.view * vec4(posOrigin, 1.0);
    vec3 cameraDir = vec3(0, 0, 0) - cameraSpace_pos.xyz; // in camera space the samera is always in position: vec3(0,0,0)
    vec3 upDir = vec3(0.0, 1.0, 0.0);
    
    // producing extruding vectors
    vec3 leftShift = normalize(cross(cameraDir, upDir));
    vec3 rightShift = -leftShift;
    
    // rotated quad that way to be perpendicular to camera direction
    vec3 billBoardQuad[4] = vec3[](
        vec3(leftShift.x, 0.0, leftShift.z),
        vec3(leftShift.x, 1.0, leftShift.z),
        vec3(rightShift.x, 0.0, rightShift.z),
        vec3(rightShift.x, 1.0, rightShift.z)
    );
    
    vec3 extrudedVector = billBoardQuad[gl_VertexIndex];
    fragTexCoord = clamp(quadPos[gl_VertexIndex], vec2(0.0), vec2(1.0));
    
    // the original postion will be shifted due to extruding (vector adding)
    vec4 pos = cameraSpace_pos + vec4(scale * extrudedVector, 1.0f);
    gl_Position = uboViewProjection.proj * pos;
    
    // preserve original depth value because fragPos is shifted due to extruding (vector adding)
    vec4 clip = uboViewProjection.viewProj * vec4(posOrigin, 1.0f);
    float ndc_z = clip.z / clip.w; // no need to execute win_z = ndc_z * 0.5 + 0.5 since we force glm to produce z range [0; 1] 
    fragDepth = ndc_z; // depth value for 2.5D (billboard) is the same for all vertices
}