#version 450

layout(location = 0) in vec4 inPosInViewSpace;

layout(location = 0) out vec4 out_viewSpacePosColor;

void main() {
  out_viewSpacePosColor = inPosInViewSpace;
  //depth will be written by itself
  //gl_FragDepth = gl_FragCoord.z;
}
