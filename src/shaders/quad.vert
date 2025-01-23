#version 450

layout(location = 0) in vec2 v_position;
layout(location = 1) in vec2 v_tex;
layout(location = 2) in vec4 v_color;

layout(location = 0) out vec2 f_tex;
layout(location = 1) out vec4 f_color;

void main() {
    f_tex = v_tex;
    f_color = v_color;
    gl_Position = vec4(v_position, 1, 1);
}
