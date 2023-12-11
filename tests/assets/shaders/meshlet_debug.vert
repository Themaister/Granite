#version 450
#extension GL_ARB_shader_draw_parameters : require

layout(location = 0) in vec3 POS;
layout(location = 1) in mediump vec3 N;
layout(location = 2) in mediump vec4 T;
layout(location = 3) in vec2 UV;

layout(location = 0) out mediump vec3 vNormal;
layout(location = 1) out mediump vec4 vTangent;
layout(location = 2) out vec2 vUV;

layout(set = 1, binding = 0) uniform UBO
{
    mat4 VP;
};

layout(set = 0, binding = 0) readonly buffer DrawParameters
{
    uvec2 data[];
} draw_info;

layout(set = 0, binding = 1) readonly buffer Transforms
{
    mat4 data[];
} transforms;

void main()
{
    mat4 M = transforms.data[draw_info.data[gl_DrawIDARB].x];

    vNormal = mat3(M) * N;
    vTangent = vec4(mat3(M) * T.xyz, T.w);
    vUV = UV;
    gl_Position = VP * (M * vec4(POS, 1.0));
}
