#version 450
layout(location = 0) out float AO;
layout(location = 0) in vec2 vUV;

#pragma optimize off

layout(set = 0, binding = 0) uniform sampler2D uDepth;
layout(set = 0, binding = 1) uniform sampler2D uNormal;

layout(std140, set = 1, binding = 0) uniform Registers
{
    mat4 view_projection;
    mat4 shadow_matrix;
    mat4 inv_view_projection;
    vec4 inv_z_transform;
    float radius;
} registers;

vec3 reconstruct_tangent(vec3 normal)
{
    vec3 up = abs(normal.y) > 0.999 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(normal, up));
    return tangent;
}

float to_world_depth(float z)
{
    vec2 zw = z * registers.inv_z_transform.xy + registers.inv_z_transform.zw;
    return -zw.x / zw.y;
}

vec3 project(vec4 c)
{
    return c.xyz / c.w;
}

float min4(vec4 v)
{
    vec2 v2 = min(v.xy, v.zw);
    return min(v2.x, v2.y);
}

layout(constant_id = 0) const float HALO_THRESHOLD = 0.1;

void main()
{
    float d = min4(textureGather(uDepth, vUV));
    if (d == 1.0)
        discard;

    vec4 clip = vec4(vUV * 2.0 - 1.0, d, 1.0);
    vec3 world = project(registers.inv_view_projection * clip);

    vec3 normal = normalize(textureLod(uNormal, vUV, 0.0).xyz * 2.0 - 1.0);
    vec3 tangent = reconstruct_tangent(normal);
    vec3 bitangent = cross(normal, tangent);

    vec3 delta_x = tangent * registers.radius;
    vec3 delta_y = bitangent * registers.radius;
    vec3 delta_z = normal * registers.radius;

    float ao = 0.0;
    float w = 0.0;

    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            // TODO: Importance-sample hemisphere?
            vec2 amp = (vec2(x, y) - 3.5) / 4.0;
            float z_amp = sqrt(max(1.0 - dot(amp, amp), 0.0));

            vec3 near_world = world +
                amp.x * delta_x +
                amp.y * delta_y +
                z_amp * delta_z;

            vec4 c = registers.shadow_matrix * vec4(near_world, 1.0);

            if (c.w >= 0.0)
            {
                vec3 C = c.xyz / c.w;
                float ref_depth = to_world_depth(C.z);
                float sampled_depth = to_world_depth(textureLod(uDepth, C.xy, 0.0).x);
                float delta = ref_depth - sampled_depth;

                if (delta < 0.0)
                {
                    ao += z_amp;
                    w += z_amp;
                }
                else
                {
                    float v = smoothstep(0.0, HALO_THRESHOLD, delta);
                    ao += v * z_amp;
                    w += z_amp;
                }
            }
        }
    }

    AO = ao / max(w, 0.001);
}