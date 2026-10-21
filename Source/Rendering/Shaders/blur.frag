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

layout(binding = 4) uniform sampler2D image;

const int MAXIMUM_TAPS = 32;

vec4 fetch(vec2 coordinates) {
  vec2 grid = 256.0 / source_rect.zw;
  coordinates = clamp(coordinates, bounds.xy, bounds.zw);
  return textureLod(image, floor(coordinates * grid + 0.5) / grid, 0.0);
}

void main() {
  vec2 coordinates = (v_position - source_rect.xy) * source_rect.zw;
  if(options.z > 0.5) {
    coordinates.y = 1.0 - coordinates.y;
  }
  vec4 color = vec4(0.0);
  if(blur.z < 0.0) {
    float total = 0.0;
    for(int i = -MAXIMUM_TAPS; i <= MAXIMUM_TAPS; ++i) {
      float tap = float(i);
      if(tap < blur.z + 0.5 || tap > blur.w) {
        continue;
      }
      color += fetch(coordinates + tap * blur.xy);
      total += 1.0;
    }
    color /= total;
  } else if(blur.w < 0.5) {
    color = fetch(coordinates);
  } else {
    float total = 0.0;
    float scale = -0.5 / (blur.z * blur.z);
    for(int i = -MAXIMUM_TAPS; i <= MAXIMUM_TAPS; ++i) {
      float tap = float(i);
      if(abs(tap) > blur.w) {
        continue;
      }
      float weight = exp(tap * tap * scale);
      color += weight * fetch(coordinates + tap * blur.xy);
      total += weight;
    }
    color /= total;
  }
  if(options.x > 0.5) {
    color = vec4(0.0, 0.0, 0.0, 1.0 - color.a);
  }
  color.rgb = clamp(color.rgb + options.y * color.a, 0.0, color.a);
  fragment_color = color;
}
