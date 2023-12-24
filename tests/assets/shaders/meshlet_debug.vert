#version 450
#extension GL_ARB_shader_draw_parameters : require

#include "meshlet_render_types.h"

layout(location = 0) in vec3 POS;
layout(location = 0) out vec3 vWorldPos;
#if !SINGLE_INSTANCE_RENDER
layout(location = 1) flat out uint vDrawID;
#endif

layout(set = 1, binding = 0) uniform UBO
{
    mat4 VP;
};

#if SINGLE_INSTANCE_RENDER
layout(set = 1, binding = 1) uniform DrawParameters
{
    mat4 M;
};
#else
layout(set = 0, binding = 0) readonly buffer DrawParameters
{
    CompactedDrawInfo data[];
} draw_info;

layout(set = 0, binding = 1) readonly buffer Transforms
{
    mat4 data[];
} transforms;
#endif

void main()
{
#if !SINGLE_INSTANCE_RENDER
    mat4 M = transforms.data[draw_info.data[gl_DrawIDARB].node_offset];
#endif
    vec3 world_pos = (M * vec4(POS, 1.0)).xyz;
    vWorldPos = world_pos;
#if !SINGLE_INSTANCE_RENDER
    vDrawID = draw_info.data[gl_DrawIDARB].meshlet_index;
#endif

    gl_Position = VP * vec4(world_pos, 1.0);
}
