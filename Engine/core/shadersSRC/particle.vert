#version 450

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
	mat4 footPrintViewProj;
    mat4 prevViewProj;
} uboViewProjection;

layout(set = 0, binding = 3) uniform UBOParticleObject {
    vec4 dynamicPos;
    vec4 velocity;
    int mode; // 0: static particles, 1: anchored particles, 2: ghost particles with per-instance spawn state
} uboParticle;

// Instance attributes

layout (location = 0) in vec3 inPosOrigin;
layout (location = 1) in vec3 scaleMin;
layout (location = 2) in vec3 scaleMax;

layout (location = 3) in vec3 inPos;
layout (location = 4) in vec3 inVelocity;
layout (location = 5) in vec3 acceleration;
layout (location = 6) in float lifeDuration;
layout (location = 7) in float alphaK;
layout (location = 8) in float birthTimeMs;

// Array for triangle that represents the quad
vec2 quadPos[4] = vec2[](
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(-1.0, 0.0),
    vec2(-1.0, 1.0)
);

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec4 lightPos; // w is elapsedMS for previous frame
    vec3 cameraPos;
    vec4 windDirElapsedTimeMS; // xyz is wind dir, w is elapsedMS
} pushConstant;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out float fragDepth;
layout(location = 2) out float kFading;
layout(location = 3) flat out int isGradientEnabled;
layout(location = 4) out float alpha;
layout(location = 5) out vec2 outMotionVector;

float hash(float value)
{
    return fract(sin(value * 91.173) * 43758.5453);
}

vec3 particleVariation(float particleId)
{
    return vec3(hash(particleId) * 2.0 - 1.0, hash(particleId + 17.0) * 2.0 - 1.0,
                hash(particleId + 43.0) * 2.0 - 1.0);
}

vec3 calculateAnchoredParticlePosition(float elapsedTimeMS, out float calculatedFading)
{
    const float fadingMultiplier = 5.0;
    calculatedFading = fract(elapsedTimeMS / lifeDuration);
    float time = fadingMultiplier * calculatedFading;
    vec3 variation = particleVariation(float(gl_InstanceIndex));
    vec3 spawnOffset = variation * vec3(0.7, 0.2, 0.7);
    vec3 drift = variation * vec3(0.22, 0.1, 0.22) * time;
    float sway = sin(time * 2.3 + variation.x * 6.2831853) * 0.15;

    return uboParticle.dynamicPos.xyz + inPosOrigin + spawnOffset + time * (uboParticle.velocity.xyz + drift) +
           time * time * acceleration + vec3(sway, 0.0, -sway);
}

vec3 calculateGhostParticlePosition(float elapsedTimeMS, out float calculatedFading)
{
    const float fadingMultiplier = 5.0;
    calculatedFading = clamp((elapsedTimeMS - birthTimeMs) / lifeDuration, 0.0, 1.0);
    float time = fadingMultiplier * calculatedFading;
    vec3 variation = particleVariation(float(gl_InstanceIndex));
    vec3 spawnOffset = variation * vec3(0.7, 0.2, 0.7);
    vec3 drift = variation * vec3(0.22, 0.1, 0.22) * time;
    float sway = sin(time * 2.3 + variation.x * 6.2831853) * 0.15;

    return inPos + inPosOrigin + spawnOffset + time * (inVelocity + drift) +
           time * time * acceleration + vec3(sway, 0.0, -sway);
}

void main()
{
    vec3 posOrigin = inPos;
    vec3 scale = scaleMax;
    // Mode 1: all particles follow the current emitter position and velocity.
    if (uboParticle.mode == 1) {
        posOrigin = calculateAnchoredParticlePosition(pushConstant.windDirElapsedTimeMS.w, kFading);
        scale = alphaK * mix(scaleMin, scaleMax, kFading);
        alpha = alphaK;
        isGradientEnabled = 1;
    // Mode 2: each particle continues from its own world-space spawn state.
    } else if (uboParticle.mode == 2) {
        posOrigin = calculateGhostParticlePosition(pushConstant.windDirElapsedTimeMS.w, kFading);
        scale = alphaK * mix(scaleMin, scaleMax, kFading);
        alpha = alphaK;
        isGradientEnabled = 1;
    // Mode 0: static particles use their original instance position.
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

    vec3 prevPosOrigin = posOrigin;
    // Use the same particle mode for the previous-frame position used by motion vectors.
    if (uboParticle.mode == 1) {
        float kFadingPrev;
        prevPosOrigin = calculateAnchoredParticlePosition(pushConstant.lightPos.w, kFadingPrev);
    } else if (uboParticle.mode == 2) {
        float kFadingPrev;
        prevPosOrigin = calculateGhostParticlePosition(pushConstant.lightPos.w, kFadingPrev);
    }

    vec4 prevClip = uboViewProjection.prevViewProj * vec4(prevPosOrigin, 1.0f);
    vec2 currentNDCPos = clip.xy / clip.w;
    vec2 prevNDCPos = prevClip.xy / prevClip.w;
    outMotionVector = currentNDCPos - prevNDCPos;
}