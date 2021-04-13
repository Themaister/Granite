#version 450

#if defined(RENDERER_FORWARD)
#include "inc/subgroup_extensions.h"
#endif

#include "inc/render_target.h"

layout(set = 2, binding = 0) uniform sampler3D uProbe;
layout(set = 3, binding = 0) uniform UBO
{
    vec3 pos;
    float radius;
    vec3 tex_coord;
};

layout(location = 0) in vec3 vNormal;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 normal2 = normal * normal;
    vec3 normal_offsets = mix(vec3(0.0), vec3(1.0 / 6.0), lessThan(normal, vec3(0.0)));

    float base_x = tex_coord.x / 6.0;
    float x_offset = base_x + (0.0 / 3.0) + normal_offsets.x;
    float y_offset = base_x + (1.0 / 3.0) + normal_offsets.y;
    float z_offset = base_x + (2.0 / 3.0) + normal_offsets.z;

    vec3 result =
        normal2.x * textureLod(uProbe, vec3(x_offset, tex_coord.yz), 0.0).rgb +
        normal2.y * textureLod(uProbe, vec3(y_offset, tex_coord.yz), 0.0).rgb +
        normal2.z * textureLod(uProbe, vec3(z_offset, tex_coord.yz), 0.0).rgb;

    emit_render_target(result, vec4(0.0), normal, 0.0, 1.0, 0.0, vec3(0.0));
}
