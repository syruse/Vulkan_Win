#version 450

layout(location = 0) in vec4 inPosInViewSpace;
layout(location = 1) in vec2 inMotionVectors;

layout(location = 0) out vec4 out_viewSpacePosColor;
layout(location = 1) out vec2 out_motionVectors;

void main() {
  out_viewSpacePosColor = inPosInViewSpace;

  // Excessively large or invalid motion vectors sometimes end up in the motion vector buffer 
  // at depth or object edges, particularly near the boundary between terrain and a wall or perimeter.
  // At an edge, a single pixel might belong to the terrain in the current frame and 
  // to a wall in the previous frame, or vice versa. 
  // Furthermore, due to camera movement, the edge shifts from one pixel to another. 
  // The motion vector becomes ill-defined in such cases: 
  // there is no reliable correspondence indicating that 
  // "this specific surface point was right here in the previous frame."
  // The current code clamps the vector length while preserving its direction.
  // clamp changes the direction (0.06;0.03 -> 0.03, 0.03)
  const float maxMotionVectorLength = 0.03;
  float motionVectorLength = length(inMotionVectors);
  out_motionVectors = motionVectorLength > maxMotionVectorLength
      ? inMotionVectors * (maxMotionVectorLength / motionVectorLength)
      : inMotionVectors;

  //depth will be written by itself
  //gl_FragDepth = gl_FragCoord.z;
}
