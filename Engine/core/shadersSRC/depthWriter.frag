#version 450

layout(location = 0) in vec4 inPosInViewSpace;
layout(location = 1) in vec2 inMotionVectors;

layout(location = 0) out vec4 out_viewSpacePosColor;
layout(location = 1) out vec2 out_motionVectors;

void main() {
  out_viewSpacePosColor = inPosInViewSpace;
  out_motionVectors = inMotionVectors;
  //depth will be written by itself
  //gl_FragDepth = gl_FragCoord.z;
}
