#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout (location = 0) in vec4 in_color;
layout (location = 1) in vec2 in_uv;
layout (location = 2) flat in uint in_img;

layout (location = 0) out vec4 out_color;

layout (set = 0, binding = 0) uniform sampler2DArray u_font_atlas[];

layout (push_constant) uniform Constants
{
    vec2 view_size_over_2;
    vec2 view_2_over_size;
    vec2 screen_pos;
    uint color;
    float color_strength;
    uint font_id;
} c;

void main()
{
    //vec2 tex_size = vec2(textureSize(u_font_atlas[c.font_id], 0).xy);
    //vec2 uv_norm = in_uv / tex_size;
    float alpha = textureLod(u_font_atlas[c.font_id], vec3(in_uv, float(in_img)), 0).r;
    out_color = vec4(in_color.rgb, in_color.a * alpha);
}

