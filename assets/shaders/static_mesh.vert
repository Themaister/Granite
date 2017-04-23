#version 310 es

layout(set = 0, binding = 0, std140) uniform RenderParameters
{
    mat4 projection;
	mat4 view;
	mat4 view_projection;
	mat4 inv_projection;
	mat4 inv_view;
	mat4 inv_view_projection;
	mat4 inv_local_view_projection;

	vec3 camera_position;
	vec3 camera_front;
	vec3 camera_right;
	vec3 camera_up;
} global;

struct StaticMeshInfo
{
    mat4 Model;
    mat4 Normal;
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
layout(location = 1) out mediump vec3 vNormal;
#endif

#if HAVE_TANGENT
layout(location = 3) in mediump vec3 Tangent;
layout(location = 2) out mediump vec3 vTangent;
#endif

void main()
{
    vec3 World =
        infos[gl_InstanceIndex].Model[0].xyz * Position.x +
        infos[gl_InstanceIndex].Model[1].xyz * Position.y +
        infos[gl_InstanceIndex].Model[2].xyz * Position.z +
        infos[gl_InstanceIndex].Model[3].xyz;
    gl_Position = global.view_projection * vec4(World, 1.0);

#if HAVE_NORMAL
    vNormal = normalize(mat3(infos[gl_InstanceIndex].Normal) * Normal);
    vTangent = normalize(mat3(infos[gl_InstanceIndex].Normal) * Tangent);
#endif

#if HAVE_UV
    vUV = UV;
#endif
}
