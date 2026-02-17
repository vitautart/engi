#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in uint col;
layout (location = 3) in uint img;

layout (location = 0) out vec4 out_color;
layout (location = 1) out vec2 out_uv;
layout (location = 2) flat out uint out_img;

layout (push_constant) uniform Constants 
{ 
    vec2 view_size_over_2; // not used for now
    vec2 view_2_over_size;
    vec2 screen_pos;
    uint color;
    float color_strength; // 0..1.0
} c;

void main()
{
    vec2 p = c.screen_pos + pos; 
    gl_Position = vec4(p * c.view_2_over_size - 1.0, 0.0, 1.0);
    out_color = color_strength * unpackUnorm4x8(color) + (1.0 - color_strength) * unpackUnorm4x8(col);
    out_uv = uv;
    out_img = img;
}

