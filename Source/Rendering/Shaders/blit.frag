#version 440

layout(location = 0) in vec2 v_coordinates;

layout(location = 0) out vec4 fragment_color;

layout(binding = 1) uniform sampler2D image;

void main() {
  fragment_color = texture(image, v_coordinates);
}
