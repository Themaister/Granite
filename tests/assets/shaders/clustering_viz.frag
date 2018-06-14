#version 450
layout(location = 0) flat in vec3 vColor;
layout(location = 1) in vec3 vPos;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec3 abs_pos = fract(abs(vPos) + 0.05);
    float max_pos = max(max(abs_pos.x, abs_pos.y), abs_pos.z);
    float min_pos = min(min(abs_pos.x, abs_pos.y), abs_pos.z);
    max_pos = max(max_pos, 1.0 - min_pos);
    FragColor = vec4(vColor.xyz *  pow(max_pos, 20.0), 1.0);
}
