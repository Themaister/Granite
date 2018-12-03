#version 450
layout(location = 0) out float AO;
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vClip;

#pragma optimize off

layout(constant_id = 0) const int KERNEL_SIZE = 64;
layout(constant_id = 1) const float HALO_THRESHOLD = 0.1;

layout(set = 0, binding = 0) uniform sampler2D uDepth;
layout(set = 0, binding = 1) uniform sampler2D uNormal;
layout(set = 0, binding = 2) uniform sampler2D uNoise;
layout(set = 0, binding = 3, std140) uniform Kernel
{
    vec3 hemisphere_kernel[KERNEL_SIZE];
};

layout(std140, set = 1, binding = 0) uniform Registers
{
    mat4 shadow_matrix;
    mat4 inv_view_projection;
    vec4 inv_z_transform;
    vec2 noise_scale;
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

void main()
{
    float d = min4(textureGather(uDepth, vUV));
    if (d == 1.0)
        discard;

    vec4 world4 = vClip + d * registers.inv_view_projection[2];
    vec3 world = project(world4);

    // Implementation heavily inspired from
    // http://john-chapman-graphics.blogspot.com/2013/01/ssao-tutorial.html

    vec3 normal = normalize(textureLod(uNormal, vUV, 0.0).xyz * 2.0 - 1.0);
    vec3 rvec = vec3(textureLod(uNoise, vUV * registers.noise_scale, 0.0).xy, 0.0);
    vec3 tangent = normalize(rvec - normal * dot(rvec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float ao = 0.0;

    for (int i = 0; i < KERNEL_SIZE; i++)
    {
        vec3 samp = tbn * hemisphere_kernel[i];
        vec3 near_world = world + HALO_THRESHOLD * samp;
        vec4 c = registers.shadow_matrix * vec4(near_world, 1.0);

        if (c.w >= 0.0)
        {
            vec3 C = c.xyz / c.w;
            float ref_depth = to_world_depth(C.z);
            float sampled_depth = to_world_depth(textureLod(uDepth, C.xy, 0.0).x);
            float delta = ref_depth - sampled_depth;

            if (delta < 0.0)
            {
                ao += 1.0;
            }
            else
            {
                float v = smoothstep(0.9 * HALO_THRESHOLD, HALO_THRESHOLD, delta);
                ao += v;
            }
        }
    }

    AO = ao / float(KERNEL_SIZE);
}