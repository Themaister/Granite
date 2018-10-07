#version 450
precision highp float;
precision highp int;

#include "../inc/render_target.h"

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec4 vGradNormalUV;

layout(set = 2, binding = 2) uniform mediump sampler2D uGradJacobian;
layout(set = 2, binding = 3) uniform mediump sampler2D uNormal;

#ifndef VARIANT_BIT_1
#define VARIANT_BIT_1 0
#endif

#ifndef VARIANT_BIT_2
#define VARIANT_BIT_2 0
#endif

#if VARIANT_BIT_1
#include "../inc/render_parameters.h"
#include "../inc/bandlimited_pixel_filter.h"

// sampler2DArray makes way more sense here.
layout(set = 2, binding = 4) uniform mediump sampler2DArray uDirectRefraction;

layout(set = 2, binding = 5, std140) uniform Refraction
{
    vec4 refraction_size;
    vec4 refraction_depths;
    float refraction_uv_scale;
    float refraction_emissive_mod;
    int refraction_layers;
};

mediump vec4 sample_refraction_layer(int layer, mediump float turbulence,
                                     mediump vec2 refraction_dir_xz, vec2 pos_xz)
{
    mediump vec2 refracted_xz_offset = refraction_dir_xz * refraction_depths[layer];
    vec2 uv = refraction_uv_scale * (refracted_xz_offset + pos_xz);
    #if VARIANT_BIT_2
        mediump float turbulence_extent_mod = exp(1.0 * turbulence);
        BandlimitedPixelInfo info = compute_pixel_weights(uv, refraction_size.xy, refraction_size.zw, turbulence_extent_mod);
        mediump vec4 result = sample_bandlimited_pixel_array(uDirectRefraction, vec3(uv, float(layer)), info, 1.0 * turbulence);
    #else
        mediump vec4 result = texture(uDirectRefraction, vec3(uv, float(layer)), 1.0 * turbulence);
    #endif
    return result;
}
#endif

void main()
{
    mediump vec3 grad_jacobian = texture(uGradJacobian, vGradNormalUV.xy).xyz;
    mediump vec2 N = 0.3 * texture(uNormal, vGradNormalUV.zw).xy;
    mediump float jacobian = grad_jacobian.z;
    mediump float turbulence = max(2.0 - jacobian + dot(abs(N), vec2(1.2)), 0.0);

    N += grad_jacobian.xy;
    mediump vec3 normal = normalize(vec3(-N.x, 1.0, -N.y));

    const vec3 base_color = vec3(1.0);

#if VARIANT_BIT_1
    mediump vec3 to_ocean = normalize(vPos - global.camera_position);
    mediump vec3 refracted_dir = refract(to_ocean, normal, 1.0 / 1.33);
    mediump float dir_to_bottom = -refracted_dir.y;

    // Fallback, assume normal is pointing straight up.
    if (dir_to_bottom <= 0.0)
    {
        refracted_dir = refract(to_ocean, vec3(0.0, 1.0, 0.0), 1.0 / 1.33);
        dir_to_bottom = -refracted_dir.y;
    }

    // Still fails, go for fallback.
    if (dir_to_bottom < 0.0)
        dir_to_bottom = 1.0;

    mediump vec2 refracted_dir_xz = refracted_dir.xz / dir_to_bottom;
    vec2 pos_xz = vPos.xz + vPos.y * refracted_dir_xz;

    int layer = refraction_layers - 1;
    mediump vec3 emissive = sample_refraction_layer(layer, turbulence, refracted_dir_xz, pos_xz).rgb;
    for (int l = layer - 1; l >= 0; l--)
    {
        mediump vec4 c = sample_refraction_layer(l, turbulence, refracted_dir_xz, pos_xz);
        emissive = c.rgb + c.a * emissive;
    }
    emissive *= refraction_emissive_mod;
#else
    const mediump vec3 emissive = vec3(0.0);
#endif

    emit_render_target(emissive, vec4(base_color, 1.0), normal,
                       1.0 - 0.2 * turbulence, 0.2 * turbulence, 1.0, vPos);
}
