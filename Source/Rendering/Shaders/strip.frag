#version 440

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_coverage;

layout(location = 0) out vec4 fragment_color;

layout(binding = 1) uniform sampler2D atlas;

void main() {
  if(v_coverage.z < 0.0) {
    fragment_color = v_color;
    return;
  }
  vec4 coverage = texture(atlas, v_coverage.xy);
  vec4 row = clamp(
    1.0 - abs(vec4(v_coverage.z) - vec4(0.5, 1.5, 2.5, 3.5)), 0.0, 1.0);
  fragment_color = v_color * dot(coverage, row);
}
