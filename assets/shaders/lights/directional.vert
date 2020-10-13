#version 450
layout(location = 0) in vec2 Position;
layout(location = 0) out highp vec4 vClip;

#define SHADOW_NUM_CASCADES 4

layout(std140, set = 0, binding = 0) uniform Transforms
{
    mat4 inverse_view_projection;
    mat4 transforms[SHADOW_NUM_CASCADES];
};

void main()
{
    gl_Position = vec4(Position, 1.0, 1.0);
    vClip = inverse_view_projection * vec4(Position, 0.0, 1.0);
}