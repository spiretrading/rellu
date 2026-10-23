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
const float KERNEL_STRIDE = 100.0;

vec4 fetch(vec2 coordinates) {
  vec2 grid = 256.0 / source_rect.zw;
  coordinates = clamp(coordinates, bounds.xy, bounds.zw);
  return textureLod(image, floor(coordinates * grid + 0.5) / grid, 0.0);
}

float choose(float n) {
  return n >= 2.0 ? 0.5 * n * (n - 1.0) : 0.0;
}

void main() {
  vec2 coordinates = (v_position - source_rect.xy) * source_rect.zw;
  if(options.z > 0.5) {
    coordinates.y = 1.0 - coordinates.y;
  }
  vec4 color = vec4(0.0);
  float kernel = floor(blur.w / KERNEL_STRIDE);
  float taps = blur.w - kernel * KERNEL_STRIDE;
  if(kernel > 2.5) {
    float alpha = fetch(coordinates).a;
    vec2 horizontal = vec2(blur.x, 0.0);
    vec2 vertical = vec2(0.0, blur.y);
    float dx = max(max(fetch(coordinates + horizontal).a,
      fetch(coordinates - horizontal).a) - alpha, 0.0);
    float dy = max(max(fetch(coordinates + vertical).a,
      fetch(coordinates - vertical).a) - alpha, 0.0);
    float grow = blur.z * length(vec2(dx, dy));
    if(grow <= 0.0) {
      vec2 diagonal = vec2(blur.x, blur.y);
      vec2 antidiagonal = vec2(blur.x, -blur.y);
      float corner = max(
        max(fetch(coordinates + diagonal).a, fetch(coordinates - diagonal).a),
        max(fetch(coordinates + antidiagonal).a,
          fetch(coordinates - antidiagonal).a));
      grow = blur.z * blur.z * max(corner - alpha, 0.0);
    }
    color = vec4(0.0, 0.0, 0.0, min(1.0, alpha + grow));
  } else if(kernel > 1.5) {
    float total = 0.0;
    for(int i = -MAXIMUM_TAPS; i <= MAXIMUM_TAPS; ++i) {
      float tap = float(i);
      if(abs(tap) > taps) {
        continue;
      }
      float u = tap / blur.z + 1.5;
      float weight = 0.0;
      if(u > 0.0 && u < 1.0) {
        weight = 0.5 * u * u;
      } else if(u >= 1.0 && u < 2.0) {
        weight = 0.5 * (-2.0 * u * u + 6.0 * u - 3.0);
      } else if(u >= 2.0 && u < 3.0) {
        weight = 0.5 * (3.0 - u) * (3.0 - u);
      }
      color += weight * fetch(coordinates + tap * blur.xy);
      total += weight;
    }
    color /= total;
  } else if(kernel > 0.5) {
    int size = int(blur.z + 0.5);
    int half_size = size / 2;
    vec3 widths = vec3(float(size));
    int base = -3 * half_size;
    if(size % 2 == 0) {
      widths.z = float(size + 1);
      base += 1;
    }
    float product = widths.x * widths.y * widths.z;
    for(int i = -MAXIMUM_TAPS; i <= MAXIMUM_TAPS; ++i) {
      float tap = float(i);
      if(abs(tap) > taps) {
        continue;
      }
      float sum = float(i - base);
      float count = choose(sum + 2.0) - choose(sum - widths.x + 2.0) -
        choose(sum - widths.y + 2.0) - choose(sum - widths.z + 2.0) +
        choose(sum - widths.x - widths.y + 2.0) +
        choose(sum - widths.x - widths.z + 2.0) +
        choose(sum - widths.y - widths.z + 2.0) -
        choose(sum - widths.x - widths.y - widths.z + 2.0);
      color += (count / product) * fetch(coordinates + tap * blur.xy);
    }
  } else if(blur.z < 0.0) {
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
