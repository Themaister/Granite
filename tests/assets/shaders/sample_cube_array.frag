#version 450

#if 0
layout(set = 0, binding = 0) uniform sampler2DArray uCube;
#elif 0
layout(set = 0, binding = 0) uniform samplerCube uCube;
#else
layout(set = 0, binding = 0) uniform samplerCubeArray uCube;
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
    FragColor = textureLod(uCube, vec3(0.5, 0.5, float(face) + 6.0 * slice), 0.0);
#elif 0
    FragColor = textureLod(uCube, directions[face], 0.0);
#else
    FragColor = textureLod(uCube, vec4(directions[face], slice), 0.0);
#endif
}