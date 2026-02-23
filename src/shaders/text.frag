#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout (location = 0) in vec4 in_color;
layout (location = 1) in vec2 in_uv;
layout (location = 2) flat in uint in_img;

layout (location = 0) out vec4 out_color;

layout (set = 0, binding = 0) uniform sampler2D u_font_atlas[];

void main()
{
    float alpha = textureLod(u_font_atlas[in_img], in_uv, 0).r;
    out_color = vec4(in_color.rgb, in_color.a * alpha);
}

