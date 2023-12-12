#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_fragment_shader_barycentric : require

#if 0
layout(location = 0) in mediump vec3 vNormal;
layout(location = 1) in mediump vec4 vTangent;
layout(location = 2) in vec2 vUV;
layout(location = 3) flat in uint MaterialOffset;
#else
layout(location = 0) pervertexEXT in vec3 vWorldPos[];
layout(location = 1) flat in uint vDrawID;
#endif

layout(location = 0) out vec3 FragColor;

//layout(set = 0, binding = 2) uniform sampler DefaultSampler;
//layout(set = 2, binding = 0) uniform texture2D Textures[];

void main()
{
    vec3 dd = fwidth(gl_BaryCoordEXT);
    float d = max(max(dd.x, dd.y), dd.z);
    float l = min(min(gl_BaryCoordEXT.x, gl_BaryCoordEXT.y), gl_BaryCoordEXT.z);

    float pixels_from_edge = l / max(d, 0.0001);
    float highlight = 1.0 - smoothstep(0.25, 0.75, pixels_from_edge);

    vec3 normal = normalize(cross(vWorldPos[1] - vWorldPos[0], vWorldPos[2] - vWorldPos[0]));
    //vec3 color = texture(sampler2D(Textures[MaterialOffset], DefaultSampler), vUV).rgb;
    FragColor = 0.1 * (0.5 * normal + 0.5);
    FragColor.rg += 0.2 * highlight;

    uint hashed = vDrawID ^ (vDrawID * 23423465);
    FragColor.r += 0.5 * float(hashed % 19) / 19.0;
    FragColor.g += 0.5 * float(hashed % 29) / 29.0;
    FragColor.b += 0.5 * float(hashed % 131) / 131.0;
}
