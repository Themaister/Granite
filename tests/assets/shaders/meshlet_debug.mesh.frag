#version 450
#extension GL_EXT_mesh_shader : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_fragment_shader_barycentric : require

#define MESHLET_VERTEX_ID

#ifdef MESHLET_VERTEX_ID
layout(location = 0) pervertexEXT in uint vVertexID[];
layout(location = 1) perprimitiveEXT flat in uint vDrawID;
#else
layout(location = 0) in mediump vec3 vNormal;
layout(location = 1) in mediump vec4 vTangent;
layout(location = 2) in vec2 vUV;
layout(location = 3) perprimitiveEXT flat in uint vDrawID;
#endif

layout(location = 0) out vec3 FragColor;

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

mediump vec4 unpack_bgr10a2(uint v)
{
    mediump ivec4 vs;
    vs.x = bitfieldExtract(int(v), 0, 10);
    vs.y = bitfieldExtract(int(v), 10, 10);
    vs.z = bitfieldExtract(int(v), 20, 10);
    vs.w = bitfieldExtract(int(v), 30, 3);
    return vec4(vs) / vec4(511.0, 511.0, 511.0, 1.0);
}

void main()
{
    uint va = vVertexID[0];
    uint vb = vVertexID[1];
    uint vc = vVertexID[2];

    TexturedAttr attr_a = attr.data[va];
    TexturedAttr attr_b = attr.data[vb];
    TexturedAttr attr_c = attr.data[vc];

    mediump vec3 coeff = gl_BaryCoordEXT;
    mediump vec3 Na = unpack_bgr10a2(attr_a.n).xyz;
    mediump vec3 Nb = unpack_bgr10a2(attr_b.n).xyz;
    mediump vec3 Nc = unpack_bgr10a2(attr_c.n).xyz;
    mediump vec3 normal = normalize(Na * coeff.x + Nb * coeff.y + Nc * coeff.z);

    FragColor = clamp(0.5 * normal + 0.5, vec3(0.0), vec3(1.0));
    FragColor = pow(FragColor, vec3(4.0));
}
