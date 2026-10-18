#version 440

layout(location = 0) in vec2 v_coordinates;

layout(location = 0) out vec4 fragment_color;

layout(binding = 0) uniform sampler2D source;

void main() {
  fragment_color = texture(source, v_coordinates);
}
