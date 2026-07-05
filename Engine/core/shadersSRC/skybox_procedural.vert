#version 450
layout(set = 0, binding = 0) uniform DynamicUBO { mat4 model; mat4 MVP; mat4 prevModel; } dynamicUBO;
layout(location = 0) in vec3 inPosition;
layout(location = 1) out vec3 vDir;
layout(location = 2) out vec2 outMotionVector;
layout(location = 3) out float outTCurrent;
layout(location = 4) out vec3 outRotateRow0;
layout(location = 5) out vec3 outRotateRow1;
layout(location = 6) out vec3 outRotateRow2;

layout(set = 0, binding = 0) uniform UBOViewProjectionObject {
    mat4 viewProj;
    mat4 viewProjInverse;
    mat4 lightViewProj;
    mat4 proj;
    mat4 view;
    mat4 footPrintViewProj;
    mat4 prevViewProj;
} uboViewProjection;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec4 lightPos; // w is elapsedMS for previous frame
    vec3 cameraPos;
    vec4 windDirElapsedTimeMS; // w is elapsedMS for current frame
} pushConstant;

#define MS_TO_SEC_MULTIPLIER 0.001
#define TIME_MULTIPLIER 0.05
#define WIND_SPEED_K 0.1

// Rotation matrix for circular wind
mat3 rotateY(float a) {
    float c = cos(a), s = sin(a);
    return mat3(c, 0, s, 0, 1, 0, -s, 0, c);
}

// Rotation matrix for X axis
mat3 rotateX(float a) {
    float c = cos(a), s = sin(a);
    return mat3(1, 0, 0, 0, c, -s, 0, s, c);
}

void main() {
    vDir = inPosition; // Pure direction vector for cloud noise sampling
    
    // 1. Calculate the REAL deformed vertex position used for rendering
    // y coord [-1;1] -> [-0.15; 0.45] to hide mountains in skybox textures
    vec3 vert = vec3(inPosition.x, 0.3 * inPosition.y + 0.15, inPosition.z);  
    vec4 currentPos = vec4(vert, 1.0);
    
    // 2. Standard position calculation using pure rotation
    mat4 skyMVP = dynamicUBO.MVP;
    skyMVP[3] = vec4(0.0, 0.0, 0.0, skyMVP[3].w); // Clear camera translation components
    gl_Position = skyMVP * currentPos;
    gl_Position.z = gl_Position.w; // Push to far plane
	
	float T_current = pushConstant.windDirElapsedTimeMS.w * MS_TO_SEC_MULTIPLIER; 
	T_current *= TIME_MULTIPLIER; // to slow down
		
	// We calculate the rotation matrix once for each vertex
    mat3 rotM = rotateY(T_current * WIND_SPEED_K);
    
    // We pass it row by row to the fragment shader.
    outRotateRow0 = rotM[0];
    outRotateRow1 = rotM[1];
    outRotateRow2 = rotM[2];
	
	// --- MOTION VECTORS CALCULATION (FOR PREVIOUS FRAME: CAMERA + WIND) ---
    float T_prev = pushConstant.lightPos.w * MS_TO_SEC_MULTIPLIER * TIME_MULTIPLIER;
    float deltaWindAngle = (T_current - T_prev) * WIND_SPEED_K;
    
    // Pass T_current to the fragment shader for cloud noise sampling
    outTCurrent = T_current;
	
    // The cloud sampling vector rotates forward, meaning pixels physically move forward on screen.
    // To find the previous pixel position, we rewind the vertex BACKWARDS against the wind (-deltaWindAngle).
    vec3 prevVert = rotateY(-deltaWindAngle) * vert;
    vec4 prevPos = vec4(prevVert, 1.0);

    // 3. Calculate position in the previous frame using the SAME deformed vertex
    mat4 skyPrevViewProj = uboViewProjection.prevViewProj * dynamicUBO.prevModel;
    skyPrevViewProj[3] = vec4(0.0, 0.0, 0.0, skyPrevViewProj[3].w); // Clear previous camera translation
    vec4 prevPosInClipSpace = skyPrevViewProj * prevPos;
    
    // 4. Calculate Motion Vector (Using the working geometry standard: current - prev)
    vec2 currentNDCPos = gl_Position.xy / max(gl_Position.w, 0.0001);
    vec2 prevNDCPos = prevPosInClipSpace.xy / max(prevPosInClipSpace.w, 0.0001);
    
    outMotionVector = currentNDCPos - prevNDCPos;
}