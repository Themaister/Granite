#version 450
layout(set = 0, binding = 0) uniform samplerCubeArray uCube;
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
    FragColor = texture(uCube, vec4(directions[face], slice));
}