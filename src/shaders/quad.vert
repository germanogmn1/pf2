#version 450

layout(location = 0) in vec2 v_position;
layout(location = 1) in vec3 v_color;

layout(location = 0) out vec4 f_color;

void main() {
    gl_Position = vec4(v_position, 1, 1);
    f_color = vec4(v_color, 1);
}
