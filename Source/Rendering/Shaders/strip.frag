#version 440

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_coverage;
layout(location = 2) in vec4 v_paint;
layout(location = 3) in vec4 v_extra;
layout(location = 4) in vec4 v_gradient;
layout(location = 5) in vec2 v_position;
layout(location = 6) in float v_index;

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
layout(binding = 2) uniform sampler2D paints;
layout(binding = 3) uniform sampler2D ramps;
layout(binding = 4) uniform sampler2D source;

const float RAMP_WIDTH = 512.0;

vec4 paint;
vec4 extra;
vec4 gradient;

vec4 fetch(float index) {
  float row = floor(index / sizes.z);
  float column = index - row * sizes.z;
  return textureLod(paints,
    vec2((column + 0.5) / sizes.z, (row + 0.5) / sizes.w), 0.0);
}

bool is_exact() {
  return bounds.w > 0.5;
}

vec2 get_position() {
  vec2 position = gl_FragCoord.xy;
  if(bounds.z > 0.0) {
    position.y = bounds.z - position.y;
  }
  return position + bounds.xy;
}

void load_paint() {
  paint = v_paint;
  extra = v_extra;
  gradient = v_gradient;
  if(!is_exact()) {
    return;
  }
  paint = vec4(0.0);
  extra = vec4(0.0);
  gradient = vec4(0.0);
  if(v_index < -0.5) {
    return;
  }
  float base = 4.0 * floor(v_index + 0.5);
  vec4 first = fetch(base);
  vec4 second = fetch(base + 1.0);
  vec2 position = get_position();
  vec3 point = vec3(position, 1.0);
  paint = vec4(dot(first.xyz, point), dot(second.xyz, point), first.w,
    second.w);
  if(first.w > 2.5) {
    vec4 third = fetch(base + 2.0);
    vec4 fourth = fetch(base + 3.0);
    extra = vec4(third.x * position.x + third.y,
      third.z * position.y + third.w, fourth.xy);
    gradient = vec4(first.x, second.x, first.y, second.y);
  } else if(first.w > 1.5) {
    extra = fetch(base + 2.0);
    gradient = fetch(base + 3.0);
  }
}

float get_coverage() {
  if(v_coverage.z < 0.0) {
    return 1.0;
  }
  vec4 coverage = textureLod(atlas, v_coverage.xy, 0.0);
  if(is_exact()) {
    return dot(coverage,
      vec4(equal(vec4(floor(v_coverage.z)), vec4(0.0, 1.0, 2.0, 3.0))));
  }
  vec4 row = clamp(
    1.0 - abs(vec4(v_coverage.z) - vec4(0.5, 1.5, 2.5, 3.5)), 0.0, 1.0);
  return dot(coverage, row);
}

float get_radial_offset() {
  vec2 position = paint.xy - extra.xy;
  vec2 direction = extra.zw - extra.xy;
  float a = dot(direction, direction) - gradient.x * gradient.x;
  float b = dot(position, direction);
  float c = dot(position, position);
  return (b - sqrt(max(b * b - a * c, 0.0))) / a;
}

vec4 sample_texture() {
  float footprint = max(length(gradient.xy * extra.zw),
    length(gradient.zw * extra.zw));
  if(paint.w > 0.5 && footprint < 0.4995) {
    vec2 texel = (floor(paint.xy * extra.zw) + 0.5) / extra.zw;
    return textureLod(source, texel, 0.0);
  }
  return textureLod(source, paint.xy, floor(log2(max(footprint, 1.0))));
}

float get_bounds_coverage() {
  vec2 size = vec2(length(vec2(gradient.x, gradient.z)),
    length(vec2(gradient.y, gradient.w)));
  vec2 distance = min(paint.xy, 1.0 - paint.xy) / max(size, 1e-6);
  vec2 coverage = clamp(distance + 0.5, 0.0, 1.0);
  return coverage.x * coverage.y;
}

vec4 get_paint() {
  if(paint.z < 0.5) {
    return vec4(1.0);
  } else if(paint.z < 2.5) {
    float offset = paint.x;
    if(paint.z > 1.5) {
      offset = get_radial_offset();
    }
    offset = clamp(offset, 0.0, 1.0);
    vec4 color = textureLod(ramps, vec2(
      (offset * (RAMP_WIDTH - 1.0) + 0.5) / RAMP_WIDTH,
      (paint.w + 0.5) / options.w), 0.0);
    return vec4(color.rgb * color.a, color.a);
  }
  vec4 color = sample_texture();
  if(paint.z > 3.5) {
    return vec4(color.a);
  }
  if(extra.x > 0.5) {
    color *= get_bounds_coverage();
  }
  return color;
}

void main() {
  load_paint();
  fragment_color = v_color * get_paint() * get_coverage();
}
