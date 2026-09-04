#version 450

layout(binding = 1) uniform sampler2D inputTexture;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 inMotionVector;
layout(location = 3) in vec3 inWorldPos;

layout(location = 0) out vec4 out_Color;
layout(location = 1) out vec2 out_motionVectors;

layout(push_constant) uniform PushConstant {
    vec4 windowSize;
    vec3 lightPos;
    vec3 cameraPos;
    vec4 particle; // xyz is wind dir, w is elapsedMS
} pushConstant;

void main() {
    vec4 diffColor = texture(inputTexture, fragTexCoord);
    // Keep the binary alpha from mip 0; filtered mipmaps set wrong mixed alpha for leaves
    float alpha = textureLod(inputTexture, fragTexCoord, 0.0).a;
    if (alpha <= 0.01) {
        discard;
    }

    // Two-sided Blinn-Phong lighting: ambient, diffuse, and specular terms.
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(pushConstant.lightPos - inWorldPos);
    vec3 viewDir = normalize(pushConstant.cameraPos - inWorldPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float diffuse = abs(dot(normal, lightDir));
    float specular = pow(max(abs(dot(normal, halfwayDir)), 0.0), 32.0);
    const float ambient = 0.8;
    const float diffuseStrength = 0.6;
    const float specularStrength = 0.04;
    diffColor.rgb *= ambient + diffuseStrength * diffuse + specularStrength * specular;

    diffColor.a = alpha;
    out_Color = diffColor;
    out_motionVectors = inMotionVector;
}
