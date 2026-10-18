#version 440

layout(location = 0) in vec2 corner;

layout(location = 0) out vec2 v_coordinates;

layout(std140, binding = 0) uniform Uniforms {
  mat4 projection;
  vec4 rectangle;
  vec4 source;
};

void main() {
  v_coordinates = mix(source.xy, source.zw, corner);
  gl_Position = projection *
    vec4(rectangle.xy + corner * rectangle.zw, 0.0, 1.0);
}
