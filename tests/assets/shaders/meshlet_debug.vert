#version 450

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

layout(set = 1, binding = 1) uniform UBOModel
{
    mat4 M;
};

void main()
{
    vNormal = mat3(M) * N;
    vTangent = vec4(mat3(M) * T.xyz, T.w);
    vUV = UV;
    gl_Position = VP * (M * vec4(POS, 1.0));
}
