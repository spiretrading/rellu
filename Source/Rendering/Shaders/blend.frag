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
layout(binding = 4) uniform sampler2D source;
layout(binding = 5) uniform sampler2D backdrop;

vec4 paint;
vec4 extra;
vec4 gradient;
vec4 limits;

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
  limits = vec4(0.0);
  if(v_index < -0.5) {
    return;
  }
  float base = 6.0 * floor(v_index + 0.5);
  limits = fetch(base + 5.0);
  if(!is_exact()) {
    return;
  }
  vec4 first = fetch(base);
  vec4 second = fetch(base + 1.0);
  vec4 third = fetch(base + 2.0);
  vec4 fourth = fetch(base + 3.0);
  vec2 position = get_position();
  vec3 point = vec3(position, 1.0);
  paint = vec4(dot(first.xyz, point), dot(second.xyz, point), first.w,
    second.w);
  extra = vec4(third.x * position.x + third.y,
    third.z * position.y + third.w, fourth.xy);
  gradient = vec4(first.x, second.x, first.y, second.y);
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

vec3 multiply(vec3 b, vec3 s) {
  return b * s;
}

vec3 screen(vec3 b, vec3 s) {
  return b + s - b * s;
}

vec3 hard_light(vec3 b, vec3 s) {
  return mix(
    multiply(b, 2.0 * s), screen(b, 2.0 * s - 1.0), step(0.5, s));
}

float color_dodge(float b, float s) {
  if(b <= 0.0) {
    return 0.0;
  } else if(s >= 1.0) {
    return 1.0;
  }
  return min(1.0, b / (1.0 - s));
}

float color_burn(float b, float s) {
  if(b >= 1.0) {
    return 1.0;
  } else if(s <= 0.0) {
    return 0.0;
  }
  return 1.0 - min(1.0, (1.0 - b) / s);
}

float soft_light(float b, float s) {
  if(s <= 0.5) {
    return b - (1.0 - 2.0 * s) * b * (1.0 - b);
  }
  float d = sqrt(b);
  if(b <= 0.25) {
    d = ((16.0 * b - 12.0) * b + 4.0) * b;
  }
  return b + (2.0 * s - 1.0) * (d - b);
}

float get_luminosity(vec3 c) {
  return dot(c, vec3(0.3, 0.59, 0.11));
}

vec3 clip_color(vec3 c) {
  float l = get_luminosity(c);
  float n = min(min(c.r, c.g), c.b);
  float x = max(max(c.r, c.g), c.b);
  if(n < 0.0) {
    c = l + (c - l) * l / (l - n);
  }
  if(x > 1.0) {
    c = l + (c - l) * (1.0 - l) / (x - l);
  }
  return c;
}

vec3 set_luminosity(vec3 c, float l) {
  return clip_color(c + (l - get_luminosity(c)));
}

float get_saturation(vec3 c) {
  return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
}

vec3 set_saturation(vec3 c, float s) {
  float n = min(min(c.r, c.g), c.b);
  float x = max(max(c.r, c.g), c.b);
  if(x <= n) {
    return vec3(0.0);
  }
  return (c - n) * s / (x - n);
}

vec3 blend(vec3 b, vec3 s, float mode) {
  if(mode < 1.5) {
    return multiply(b, s);
  } else if(mode < 2.5) {
    return screen(b, s);
  } else if(mode < 3.5) {
    return hard_light(s, b);
  } else if(mode < 4.5) {
    return min(b, s);
  } else if(mode < 5.5) {
    return max(b, s);
  } else if(mode < 6.5) {
    return vec3(color_dodge(b.r, s.r), color_dodge(b.g, s.g),
      color_dodge(b.b, s.b));
  } else if(mode < 7.5) {
    return vec3(color_burn(b.r, s.r), color_burn(b.g, s.g),
      color_burn(b.b, s.b));
  } else if(mode < 8.5) {
    return hard_light(b, s);
  } else if(mode < 9.5) {
    return vec3(soft_light(b.r, s.r), soft_light(b.g, s.g),
      soft_light(b.b, s.b));
  } else if(mode < 10.5) {
    return abs(b - s);
  } else if(mode < 11.5) {
    return b + s - 2.0 * b * s;
  } else if(mode < 12.5) {
    return set_luminosity(
      set_saturation(s, get_saturation(b)), get_luminosity(b));
  } else if(mode < 13.5) {
    return set_luminosity(
      set_saturation(b, get_saturation(s)), get_luminosity(b));
  } else if(mode < 14.5) {
    return set_luminosity(s, get_luminosity(b));
  }
  return set_luminosity(b, get_luminosity(s));
}

void main() {
  load_paint();
  vec2 point = paint.xy;
  if(limits.z > limits.x) {
    point = clamp(point, limits.xy, limits.zw);
  }
  vec4 s = textureGrad(source, point, gradient.xy, gradient.zw);
  vec4 b = textureLod(backdrop, extra.xy, 0.0);
  if(paint.w > 15.5) {
    vec4 shadow = v_color * s.a * b.a;
    fragment_color =
      mix(b, shadow + b * (1.0 - shadow.a), get_coverage());
    return;
  }
  vec3 cs = s.a > 0.0 ? s.rgb / s.a : vec3(0.0);
  vec3 cb = b.a > 0.0 ? b.rgb / b.a : vec3(0.0);
  vec3 mixed = clamp(blend(cb, cs, paint.w), 0.0, 1.0);
  vec4 result = vec4(s.rgb * (1.0 - b.a) + b.rgb * (1.0 - s.a) +
    s.a * b.a * mixed, s.a + b.a - s.a * b.a);
  fragment_color = mix(b, result, v_color.a * get_coverage());
}
