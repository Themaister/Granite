#version 450
#extension GL_ARB_shader_draw_parameters : require

layout(location = 0) in vec3 POS;
#if 0
layout(location = 1) in mediump vec3 N;
layout(location = 2) in mediump vec4 T;
layout(location = 3) in vec2 UV;
#endif

#if 0
layout(location = 0) out mediump vec3 vNormal;
layout(location = 1) out mediump vec4 vTangent;
layout(location = 2) out vec2 vUV;
layout(location = 3) flat out uint MaterialOffset;
#else
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) flat out uint vDrawID;
#endif

layout(set = 1, binding = 0) uniform UBO
{
    mat4 VP;
};

struct CompactedDrawInfo
{
    uint node_offset;
    uint node_count_material_offset;
    uint meshlet_id;
};

layout(set = 0, binding = 0) readonly buffer DrawParameters
{
    CompactedDrawInfo data[];
} draw_info;

layout(set = 0, binding = 1) readonly buffer Transforms
{
    mat4 data[];
} transforms;

void main()
{
    mat4 M = transforms.data[draw_info.data[gl_DrawIDARB].node_offset];
    vec3 world_pos = (M * vec4(POS, 1.0)).xyz;
    vWorldPos = world_pos;
    vDrawID = draw_info.data[gl_DrawIDARB].meshlet_id;

    gl_Position = VP * vec4(world_pos, 1.0);
    #if 0
    vNormal = mat3(M) * N;
    vTangent = vec4(mat3(M) * T.xyz, T.w);
    vUV = UV;
    MaterialOffset = bitfieldExtract(draw_info.data[gl_DrawIDARB].node_count_material_offset, 8, 24);
    #endif
}
