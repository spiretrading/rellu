#version 440

layout(location = 0) in vec2 corner;
layout(location = 1) in vec4 rectangle;
layout(location = 2) in vec4 color;
layout(location = 3) in vec3 origin;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec3 v_coverage;
layout(location = 2) out vec4 v_paint;
layout(location = 3) out vec4 v_extra;
layout(location = 4) out vec4 v_gradient;
layout(location = 5) out vec2 v_position;
layout(location = 6) out float v_index;
layout(location = 7) out float v_divisor;

layout(std140, binding = 0) uniform Uniforms {
  mat4 projection;
  vec4 sizes;
  vec4 source_rect;
  vec4 blur;
  vec4 options;
  vec4 bounds;
};

layout(binding = 2) uniform sampler2D paints;

vec4 fetch(float index) {
  float row = floor(index / sizes.z);
  float column = index - row * sizes.z;
  return textureLod(paints,
    vec2((column + 0.5) / sizes.z, (row + 0.5) / sizes.w), 0.0);
}

void main() {
  vec2 position = rectangle.xy + corner * rectangle.zw;
  gl_Position = projection * vec4(position, 0.0, 1.0);
  v_color = color;
  v_position = position;
  v_index = origin.z;
  if(origin.x < 0.0) {
    v_coverage = vec3(0.0, 0.0, -1.0);
  } else {
    v_coverage = vec3(
      (origin.xy + vec2(corner.x * rectangle.z, 0.5)) / sizes.xy,
      corner.y * rectangle.w);
  }
  v_paint = vec4(0.0);
  v_extra = vec4(0.0);
  v_gradient = vec4(0.0);
  v_divisor = 1.0;
  if(origin.z >= 0.0) {
    float base = 5.0 * origin.z;
    vec4 first = fetch(base);
    vec4 second = fetch(base + 1.0);
    vec3 point = vec3(position, 1.0);
    v_divisor = dot(fetch(base + 4.0).xyz, point) + 1.0;
    v_paint = vec4(dot(first.xyz, point), dot(second.xyz, point), first.w,
      second.w);
    if(first.w > 2.5) {
      vec4 third = fetch(base + 2.0);
      vec4 fourth = fetch(base + 3.0);
      v_extra = vec4(third.x * position.x + third.y,
        third.z * position.y + third.w, fourth.xy);
      v_gradient = vec4(first.x, second.x, first.y, second.y);
    } else if(first.w > 1.5) {
      v_extra = fetch(base + 2.0);
      v_gradient = fetch(base + 3.0);
    }
  }
}
