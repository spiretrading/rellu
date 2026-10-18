#version 440

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_coverage;
layout(location = 2) in vec4 v_paint;
layout(location = 3) in vec4 v_extra;
layout(location = 4) in vec4 v_gradient;
layout(location = 5) in vec2 v_position;

layout(location = 0) out vec4 fragment_color;

layout(std140, binding = 0) uniform Uniforms {
  mat4 projection;
  vec4 sizes;
  vec4 source_rect;
  vec4 blur;
  vec4 options;
  vec4 bounds;
};

layout(binding = 1) uniform sampler2D atlas;
layout(binding = 3) uniform sampler2D ramps;
layout(binding = 4) uniform sampler2D source;

const float RAMP_WIDTH = 512.0;

float get_coverage() {
  if(v_coverage.z < 0.0) {
    return 1.0;
  }
  vec4 coverage = textureLod(atlas, v_coverage.xy, 0.0);
  vec4 row = clamp(
    1.0 - abs(vec4(v_coverage.z) - vec4(0.5, 1.5, 2.5, 3.5)), 0.0, 1.0);
  return dot(coverage, row);
}

float get_radial_offset() {
  vec2 position = v_paint.xy - v_extra.xy;
  vec2 direction = v_extra.zw - v_extra.xy;
  float a = dot(direction, direction) - v_gradient.x * v_gradient.x;
  float b = dot(position, direction);
  float c = dot(position, position);
  return (b - sqrt(max(b * b - a * c, 0.0))) / a;
}

vec4 sample_texture() {
  float footprint = max(length(v_gradient.xy * v_extra.zw),
    length(v_gradient.zw * v_extra.zw));
  return textureLod(source, v_paint.xy, floor(log2(max(footprint, 1.0))));
}

float get_bounds_coverage() {
  vec2 size = vec2(length(vec2(v_gradient.x, v_gradient.z)),
    length(vec2(v_gradient.y, v_gradient.w)));
  vec2 distance = min(v_paint.xy, 1.0 - v_paint.xy) / max(size, 1e-6);
  vec2 coverage = clamp(distance + 0.5, 0.0, 1.0);
  return coverage.x * coverage.y;
}

vec4 get_paint() {
  if(v_paint.z < 0.5) {
    return vec4(1.0);
  } else if(v_paint.z < 2.5) {
    float offset = v_paint.x;
    if(v_paint.z > 1.5) {
      offset = get_radial_offset();
    }
    offset = clamp(offset, 0.0, 1.0);
    vec4 color = textureLod(ramps, vec2(
      (offset * (RAMP_WIDTH - 1.0) + 0.5) / RAMP_WIDTH,
      (v_paint.w + 0.5) / options.w), 0.0);
    return vec4(color.rgb * color.a, color.a);
  }
  vec4 color = sample_texture();
  if(v_paint.z > 3.5) {
    return vec4(color.a);
  }
  if(v_extra.x > 0.5) {
    color *= get_bounds_coverage();
  }
  return color;
}

void main() {
  fragment_color = v_color * get_paint() * get_coverage();
}
