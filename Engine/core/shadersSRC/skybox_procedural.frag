#version 450
layout(location = 1) in vec3 vDir;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
    vec4 windDirElapsedTimeMS; // xyz is wind dir, w is elapsedMS
} pushConstant;

// --- 3D NOISE WITH TIME MORPHING ---
float hash(float n) { return fract(sin(n) * 43758.5453); }

float noise(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);
    f = f*f*(3.0-2.0*f);
    float n = p.x + p.y*57.0 + 113.0*p.z;
    return mix(mix(mix(hash(n+0.0), hash(n+1.0), f.x),
                   mix(hash(n+57.0), hash(n+58.0), f.x), f.y),
               mix(mix(hash(n+113.0), hash(n+114.0), f.x),
                   mix(hash(n+170.0), hash(n+171.0), f.x), f.y), f.z);
}

// Fractional Brownian Motion
float fbm(vec3 p, float t) {
    float v = 0.0; float amp = 0.5;
    // We add 't' to the coordinates to change shape over time
    for (int i = 0; i < 5; i++) {
        v += noise(p + t) * amp; 
        p *= 2.5; 
        amp *= 0.5;
        t *= 1.2; // Morph faster at higher frequencies
    }
    return v;
}

// Rotation matrix for circular wind
mat3 rotateY(float a) {
    float c = cos(a), s = sin(a);
    return mat3(c, 0, s, 0, 1, 0, -s, 0, c);
}

// Rotation matrix for X axis
//mat3 rotateX(float a) {
//    float c = cos(a), s = sin(a);
//    return mat3(1, 0, 0, 0, c, -s, 0, s, c);
//}

void main() {
    vec3 dir = normalize(vDir);
    float T = pushConstant.windDirElapsedTimeMS.w * 0.001;// to seconds
	T *= 0.05; // to slow down
    
    // 1. Fix orientation (clouds up)
    float skyY = -dir.y; 

    // 2. Setup sampling direction
    // vec3 sampleDir = normalize(vec3(dir.x, skyY, dir.z));
	// 3. Rotate
	// sampleDir = rotateY(T * 0.1) * sampleDir;
    // Rotate the FULL original vector
    // This fixed the "pole" distortion at the top (zenith)
    vec3 sampleDir = rotateY(T * 0.1) * dir;
    
    // 4. Cloud FBM with Morphing
    // We pass T * 0.2 as a "morph" factor to change the shape ("boiling")
    // Increased scale to [9.0:12.0] to make clouds smaller
    float n = fbm(sampleDir * 10.5, T * 1.1); 

    // 5. Colors and Finalizing
    vec3 skyColor = mix(vec3(0.4, 0.6, 0.9), vec3(0.1, 0.3, 0.7), clamp(skyY, 0.0, 1.0));
    
    // Horizon Fade (Only at the bottom) Gradient
    // -0.5: fading starts slightly below horizon
    //  0.1: clouds become fully opaque only after this height
    float horizonFade = smoothstep(-0.5, 0.1, dir.y);
    // Density: 0.55 - 0.7 gives nice separated clouds
	// 0.45 is the point where cloud starts, 0.65 is full white
    float cloudMask = smoothstep(0.45, 0.65, n) * horizonFade;
    
    vec3 col = mix(skyColor, vec3(1.0), cloudMask);

    // 6. Sun
    vec3 sunDir = normalize(vec3(0.8, 0.6, -1.0));
    float sunDot = max(dot(vec3(dir.x, skyY, dir.z), sunDir), 0.0);
    col += vec3(1.0, 0.8, 0.5) * pow(sunDot, 400.0);

    outColor = vec4(pow(max(col, 0.0), vec3(0.4545)), 1.0);
}