#version 450
layout(set = 0, binding = 0) uniform samplerCubeArrayShadow uCube;
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
    FragColor.z = texture(uCube, vec4(directions[face], slice), 0.25);
    FragColor.y = texture(uCube, vec4(directions[face], slice), 0.50);
    FragColor.x = texture(uCube, vec4(directions[face], slice), 0.75);
    FragColor.w = texture(uCube, vec4(directions[face], slice), 1.0);
}