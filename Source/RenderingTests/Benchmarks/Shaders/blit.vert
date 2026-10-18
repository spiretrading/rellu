#version 440

layout(location = 0) in vec4 vertex;

layout(location = 0) out vec2 v_coordinates;

void main() {
  gl_Position = vec4(vertex.xy, 0.0, 1.0);
  v_coordinates = vertex.zw;
}
