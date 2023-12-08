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

void main()
{
    vNormal = N;
    vTangent = T;
    vUV = UV;
    gl_Position = VP * vec4(POS, 1.0);
}
