#version 450

#if 0
layout(set = 0, binding = 0) uniform sampler2DArray uCube;
#else
layout(set = 0, binding = 0) uniform sampler2DArrayShadow uCube;
#endif

layout(location = 0) out vec4 FragColor;

const vec3 directions[] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(-1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, -1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(0.0, 0.0, -1.0));

void main()
{
    int face = int(gl_FragCoord.x);
    float slice = floor(gl_FragCoord.y);
#if 0
    FragColor = texture(uCube, vec3(0.5, 0.5, float(face) + 6.0 * slice)).xxxx;
#else
    FragColor.z = texture(uCube, vec4(0.5, 0.5, float(face) + 6.0 * slice, 0.25));
    FragColor.y = texture(uCube, vec4(0.5, 0.5, float(face) + 6.0 * slice, 0.50));
    FragColor.x = texture(uCube, vec4(0.5, 0.5, float(face) + 6.0 * slice, 0.75));
    FragColor.w = texture(uCube, vec4(0.5, 0.5, float(face) + 6.0 * slice, 1.00));
#endif
}