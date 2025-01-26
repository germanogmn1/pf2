#version 450

layout(location = 0) in vec2 f_tex;
layout(location = 1) in vec4 f_color;

layout(set = 2, binding = 0) uniform sampler2D image;

layout(location = 0) out vec4 color;

void main() {
    vec4 texel = texture(image, f_tex);
    texel.rgb *= f_color.rgb;
    color = texel;
}
