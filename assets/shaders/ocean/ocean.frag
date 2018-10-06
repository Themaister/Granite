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

#if VARIANT_BIT_2
    mediump float turbulence_extent_mod = exp(2.0 * turbulence);
    int layer = refraction_layers - 1;
    mediump float depth = (refraction_depths[layer] + vPos.y) / dir_to_bottom;
    mediump vec2 refracted_xz_offset = refracted_dir.xz * depth;
    vec2 uv = refraction_uv_scale * (refracted_xz_offset + vPos.xz);
    BandlimitedPixelInfo info = compute_pixel_weights(uv, refraction_size.xy, refraction_size.zw, turbulence_extent_mod);
    mediump vec3 emissive = sample_bandlimited_pixel_array(uDirectRefraction, vec3(uv, float(layer)), info, 2.0 * turbulence).rgb;

    for (int l = layer - 1; l >= 0; l--)
    {
        mediump float depth = (refraction_depths[l] + vPos.y) / dir_to_bottom;
        mediump vec2 refracted_xz_offset = refracted_dir.xz * depth;
        vec2 uv = refraction_uv_scale * (refracted_xz_offset + vPos.xz);
        BandlimitedPixelInfo info = compute_pixel_weights(uv, refraction_size.xy, refraction_size.zw, turbulence_extent_mod);
        mediump vec4 c = sample_bandlimited_pixel_array(uDirectRefraction, vec3(uv, float(l)), info, 2.0 * turbulence);
        emissive = mix(c.rgb, emissive, c.a);
    }
    emissive *= refraction_emissive_mod;
#else
    int layer = refraction_layers - 1;
    mediump float depth = (refraction_depths[layer] + vPos.y) / dir_to_bottom;
    mediump vec2 refracted_xz_offset = refracted_dir.xz * depth;
    vec2 uv = refraction_uv_scale * (refracted_xz_offset + vPos.xz);
    mediump vec3 emissive = texture(uDirectRefraction, vec3(uv, float(layer)), 2.0 * turbulence).rgb;

    for (int l = layer - 1; l >= 0; l--)
    {
        mediump float depth = (refraction_depths[l] + vPos.y) / dir_to_bottom;
        mediump vec2 refracted_xz_offset = refracted_dir.xz * depth;
        vec2 uv = refraction_uv_scale * (refracted_xz_offset + vPos.xz);
        mediump vec4 c = texture(uDirectRefraction, vec3(uv, float(l)), 2.0 * turbulence);
        emissive = mix(c.rgb, emissive, c.a);
    }
    emissive *= refraction_emissive_mod;
#endif
#else
    const mediump vec3 emissive = vec3(0.0);
#endif

    emit_render_target(emissive, vec4(base_color, 1.0), normal,
                       1.0 - 0.2 * turbulence, 0.2 * turbulence, 1.0, vPos);
}
