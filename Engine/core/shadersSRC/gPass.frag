#version 450

// 2D Array of textures: diffuse, bump(optional)
layout(binding = 1) uniform sampler2DArray texSampler;

layout(location = 0)
in VS_OUT {
    vec2 TexCoord;
    float isBumpMapping;
    mat3 TBN;
} fs_in;

layout(location = 0) out vec4 out_Color; // not used in g-pass
layout(location = 1) out vec4 out_GPass[2];

void main() {
  vec3 normal = vec3(0.0, 0.0, 0.0);
  if (fs_in.isBumpMapping > 0.0) {
    normal = texture(texSampler, vec3(fs_in.TexCoord, 1.0)).rgb;
    // from [0, 1] to [-1,1]
    normal = normal * 2.0 - 1.0;  
    // from texture to world orientation  
    normal = fs_in.TBN * normal;
  } else {
    normal = fs_in.TBN[2];
  }
  // Normals, pack -1, +1 range to 0, 1.
  out_GPass[0] = vec4(0.5 * normalize(normal) + 0.5, 1.0);
  out_GPass[1] = texture(texSampler, vec3(fs_in.TexCoord, 0.0));
}
