#version 450
#extension GL_EXT_mesh_shader : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_fragment_shader_barycentric : require

#if MESHLET_VERTEX_ID
layout(location = 0) pervertexEXT in uint vVertexID[];
layout(location = 1) perprimitiveEXT flat in uint vTransformIndex;
layout(location = 2) perprimitiveEXT flat in uint vDrawID;

struct TexturedAttr
{
    uint n;
    uint t;
    vec2 uv;
};

layout(set = 0, binding = 2, std430) readonly buffer VBOATTR
{
    TexturedAttr data[];
} attr;

layout(set = 0, binding = 5, std430) readonly buffer Transforms
{
    mat4 data[];
} transforms;
#else
layout(location = 0) in mediump vec3 vNormal;
layout(location = 1) in mediump vec4 vTangent;
layout(location = 2) in vec2 vUV;
layout(location = 3) perprimitiveEXT flat in uint vDrawID;
#endif

layout(location = 0) out vec3 FragColor;

mediump vec3 decode_rgb10a2(uint v)
{
    mediump ivec3 iv;
    iv.x = bitfieldExtract(int(v), 0, 10);
    iv.y = bitfieldExtract(int(v), 10, 10);
    iv.z = bitfieldExtract(int(v), 20, 10);
    return vec3(iv) / 511.0;
}

void main()
{
#if MESHLET_VERTEX_ID
    uint va = vVertexID[0];
    uint vb = vVertexID[1];
    uint vc = vVertexID[2];
    uint na = attr.data[va].n;
    uint nb = attr.data[vb].n;
    uint nc = attr.data[vc].n;

    mediump vec3 normal = gl_BaryCoordEXT.x * decode_rgb10a2(na) +
        gl_BaryCoordEXT.y * decode_rgb10a2(nb) +
        gl_BaryCoordEXT.z * decode_rgb10a2(nc);
    normal = mat3(transforms.data[vTransformIndex]) * normal;
    normal = normalize(normal);
#else
    vec3 normal = normalize(vNormal);
#endif

    FragColor = clamp(0.5 * normal + 0.5, vec3(0.0), vec3(1.0));
    FragColor = pow(FragColor, vec3(4.0));
}
