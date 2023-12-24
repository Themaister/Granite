#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_fragment_shader_barycentric : require

layout(location = 0) pervertexEXT in vec3 vWorldPos[];

#if !SINGLE_INSTANCE_RENDER
layout(location = 1) flat in uint vDrawID;
#else
struct CompactedDrawInfo { uint meshlet_index; uint node_offset; uint material_index; };
layout(push_constant) uniform Registers
{
    CompactedDrawInfo draw;
} registers;
#endif

layout(location = 0) out vec3 FragColor;

void main()
{
#if SINGLE_INSTANCE_RENDER
    uint vDrawID = registers.draw.meshlet_index;
#endif

    vec3 dd = fwidth(gl_BaryCoordEXT);
    float d = max(max(dd.x, dd.y), dd.z);
    float l = min(min(gl_BaryCoordEXT.x, gl_BaryCoordEXT.y), gl_BaryCoordEXT.z);

    float pixels_from_edge = l / max(d, 0.0001);
    float highlight = 1.0 - smoothstep(0.25, 0.75, pixels_from_edge);

    vec3 normal = normalize(cross(vWorldPos[1] - vWorldPos[0], vWorldPos[2] - vWorldPos[0]));

    FragColor = 0.5 * (0.5 * normal + 0.5);
    FragColor.rg += 0.05 * highlight;

    uint hashed = vDrawID ^ (vDrawID * 23423465);
    FragColor.r += 0.01 * float(hashed % 19) / 19.0;
    FragColor.g += 0.01 * float(hashed % 29) / 29.0;
    FragColor.b += 0.01 * float(hashed % 131) / 131.0;
}
