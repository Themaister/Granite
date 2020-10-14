#version 450
layout(location = 0) out mediump float AO;
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vClip;

layout(constant_id = 0) const int KERNEL_SIZE = 16;
layout(constant_id = 1) const float HALO_THRESHOLD = 0.1;

layout(set = 0, binding = 0) uniform sampler2D uDepth;
#if NORMAL
layout(set = 0, binding = 1) uniform mediump sampler2D uNormal;
#endif
layout(set = 0, binding = 2) uniform mediump sampler2D uNoise;
layout(set = 0, binding = 3, std140) uniform Kernel
{
    vec3 hemisphere_kernel[KERNEL_SIZE];
};

layout(std140, set = 1, binding = 0) uniform Registers
{
    mat4 shadow_matrix;
    mat4 inv_view_projection;
    vec4 inv_z_transform;
    vec4 dx_clip;
    vec4 dy_clip;
    vec2 noise_scale;
} registers;

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
    vec4 d4 = textureGather(uDepth, vUV);
    float d = min4(d4);
    if (d == 1.0)
        discard;

    vec4 world4 = vClip + d * registers.inv_view_projection[2];
    vec3 world = project(world4);

    // Implementation heavily inspired from
    // http://john-chapman-graphics.blogspot.com/2013/01/ssao-tutorial.html

#if NORMAL
    mediump vec3 normal = normalize(textureLod(uNormal, vUV, 0.0).xyz * 2.0 - 1.0);
#else
    vec2 dzdx2 = d4.zy - d4.wx;
    vec2 dzdy2 = d4.xy - d4.wz;
    float dzdx = min(dzdx2.x, dzdx2.y);
    float dzdy = min(dzdy2.x, dzdy2.y);
    vec3 world_dx = project(world4 + registers.dx_clip + dzdx * registers.inv_view_projection[2]);
    vec3 world_dy = project(world4 + registers.dy_clip + dzdy * registers.inv_view_projection[2]);
    world_dx -= world;
    world_dy -= world;
    mediump vec3 normal = normalize(cross(world_dy, world_dx));
#endif

    mediump vec3 rvec = vec3(textureLod(uNoise, vUV * registers.noise_scale, 0.0).xy, 0.0);
    mediump vec3 tangent = normalize(rvec - normal * dot(rvec, normal));
    mediump vec3 bitangent = cross(normal, tangent);
    mediump mat3 tbn = mat3(tangent, bitangent, normal);

    mediump float ao = 0.0;

    for (int i = 0; i < KERNEL_SIZE; i++)
    {
        mediump vec3 samp = tbn * hemisphere_kernel[i];
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
                mediump float v = smoothstep(0.9 * HALO_THRESHOLD, HALO_THRESHOLD, delta);
                ao += v;
            }
        }
    }

    AO = ao / float(KERNEL_SIZE);
}
