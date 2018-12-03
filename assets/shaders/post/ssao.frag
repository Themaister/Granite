#version 450
layout(location = 0) out float AO;
layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0) uniform sampler2D uDepth;
layout(set = 0, binding = 1) uniform sampler2D uNormal;

layout(std430, push_constant) uniform Registers
{
    mat4 VP;
    vec4 inv_z_transform;
    float radius;
} registers;

vec3 reconstruct_tangent(vec3 normal)
{
    vec3 up = normal.zxy;
    vec3 tangent = normalize(cross(normal, up));
    return tangent;
}

float to_world_depth(float z)
{
    vec2 zw = z * registers.inv_z_transform.xy + registers.inv_z_transform.zw;
    return zw.x / zw.y;
}

const float HALO_THRESHOLD = 0.1;

void main()
{
    float d = textureLod(uDepth, vUV, 0.0).x;
    if (d == 1.0)
        discard;

    vec3 normal = normalize(textureLod(uNormal, vUV, 0.0).xyz * 2.0 - 1.0);
    vec3 tangent = reconstruct_tangent(normal);
    vec3 bitangent = cross(normal, tangent);

    vec4 clip = vec4(vUV * 2.0 - 1.0, d, 1.0);
    vec4 delta_clip_x = registers.VP * vec4(tangent * registers.radius, 1.0);
    vec4 delta_clip_y = registers.VP * vec4(bitangent * registers.radius, 1.0);
    vec4 delta_clip_z = registers.VP * vec4(normal * registers.radius, 1.0);

    float ao = 0.0;
    float w = 0.0;

    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            // TODO: Importance-sample hemisphere?
            vec2 amp = (vec2(x, y) - 3.5) / 4.0;
            float z_amp = sqrt(max(1.0 - dot(amp, amp), 0.0));

            vec4 c = clip +
                amp.x * delta_clip_x +
                amp.y * delta_clip_y +
                z_amp * delta_clip_z;

            if (c.w >= 0.0)
            {
                vec3 C = c.xyz / c.w;
                float ref_depth = to_world_depth(C.z);
                float sampled_depth = to_world_depth(textureLod(uDepth, 0.5 * C.xy + 0.5, 0.0).x);
                float delta = ref_depth - sampled_depth;

                if (ref_depth < 0.0)
                {
                    ao += z_amp;
                    w += z_amp;
                }
                else
                {
                    float v = smoothstep(delta, 0.0, HALO_THRESHOLD);
                    ao += v * z_amp;
                    w += z_amp;
                }
            }
        }
    }

    AO = ao / max(w, 0.001);
}