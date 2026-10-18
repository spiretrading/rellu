#version 440

layout(location = 0) in vec2 corner;
layout(location = 1) in vec4 rectangle;
layout(location = 2) in vec4 color;
layout(location = 3) in vec3 origin;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec3 v_coverage;

layout(std140, binding = 0) uniform Uniforms {
  mat4 projection;
  vec4 sizes;
  vec4 source_rect;
  vec4 blur;
  vec4 options;
  vec4 bounds;
};

void main() {
  gl_Position =
    projection * vec4(rectangle.xy + corner * rectangle.zw, 0.0, 1.0);
  v_color = color;
  if(origin.x < 0.0) {
    v_coverage = vec3(0.0, 0.0, -1.0);
  } else {
    v_coverage = vec3(
      (origin.xy + vec2(corner.x * rectangle.z, 0.5)) / sizes.xy,
      corner.y * rectangle.w);
  }
}
