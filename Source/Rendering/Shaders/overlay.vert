#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 local;
layout(location = 2) in vec4 shape;
layout(location = 3) in vec4 fill;
layout(location = 4) in vec4 stroke;

layout(location = 0) out vec2 v_local;
layout(location = 1) out vec4 v_shape;
layout(location = 2) out vec4 v_fill;
layout(location = 3) out vec4 v_stroke;

layout(std140, binding = 0) uniform Uniforms {
  mat4 projection;
};

void main() {
  v_local = local;
  v_shape = shape;
  v_fill = fill;
  v_stroke = stroke;
  gl_Position = projection * vec4(position, 0.0, 1.0);
}
