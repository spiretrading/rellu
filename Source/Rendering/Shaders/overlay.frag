#version 440

layout(location = 0) in vec2 v_local;
layout(location = 1) in vec4 v_shape;
layout(location = 2) in vec4 v_fill;
layout(location = 3) in vec4 v_stroke;

layout(location = 0) out vec4 fragment_color;

float get_distance(vec2 point, vec2 half_size, float radius) {
  vec2 q = abs(point) - half_size + radius;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
  float distance = get_distance(v_local, v_shape.xy, v_shape.z);
  float outer = clamp(0.5 - distance, 0.0, 1.0);
  float inner = clamp(0.5 - distance - v_shape.w, 0.0, 1.0);
  fragment_color = v_fill * inner + v_stroke * (outer - inner);
}
