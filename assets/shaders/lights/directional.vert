#version 450
layout(location = 0) in vec2 Position;
layout(location = 0) out highp vec4 vClip;
layout(location = 1) out highp vec4 vShadowClip;
layout(location = 2) out highp vec4 vShadowNearClip;

layout(std140, set = 0, binding = 0) uniform Transforms
{
    mat4 inverse_view_projection;
    mat4 shadow_transform;
    mat4 shadow_transform_near;
};

void main()
{
    gl_Position = vec4(Position, 1.0, 1.0);
    vClip = inverse_view_projection * vec4(Position, 0.0, 1.0);
    vShadowClip = shadow_transform * vec4(Position, 0.0, 1.0);
    vShadowNearClip = shadow_transform_near * vec4(Position, 0.0, 1.0);
}