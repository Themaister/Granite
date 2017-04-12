#version 310 es

struct StaticMeshInfo
{
    mat4 MVP;
    mat3 Normal;
};

layout(set = 3, binding = 0, std140) uniform PerVertexData
{
    StaticMeshInfo infos[256];
};

layout(location = 0) in highp vec4 Position;

#if HAVE_UV
layout(location = 1) in highp vec2 UV;
layout(location = 0) out highp vec2 vUV;
#endif

#if HAVE_NORMAL
layout(location = 2) in mediump vec3 Normal;
layout(location = 1) out mediump vec2 vNormal;
#endif

#if HAVE_TANGENT
layout(location = 3) in mediump vec3 Tangent;
layout(location = 2) out mediump vec2 vTangent;
#endif

void main()
{
    gl_Position = infos[gl_InstanceIndex].MVP * Position;
    vNormal = normalize(infos[gl_InstanceIndex].Normal * Normal);
    vTangent = normalize(infos[gl_InstanceIndex].Normal * Tangent);
    vUV = UV;
}
