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

// sampler2DArray makes way more sense here. TODO: Add support for layers in render graph.
layout(set = 2, binding = 4) uniform mediump sampler2D uDirectRefraction0;
layout(set = 2, binding = 5) uniform mediump sampler2D uDirectRefraction1;
layout(set = 2, binding = 6) uniform mediump sampler2D uDirectRefraction2;
layout(set = 2, binding = 7) uniform mediump sampler2D uDirectRefraction3;

layout(set = 2, binding = 8, std140) uniform Refraction
{
    vec4 refraction_size;
    vec4 refraction_depths;
    float refraction_uv_scale;
    float refraction_emissive_mod;
};

mediump vec3 merge_emissive_layers(mediump vec4 layer0, mediump vec4 layer1, mediump vec4 layer2, mediump vec4 layer3)
{
    mediump vec3 merged = mix(layer2.rgb, layer3.rgb, layer2.a);
    merged = mix(layer1.rgb, merged, layer1.a);
    merged = mix(layer0.rgb, merged, layer0.a);
    return merged;
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
    vec2 uv0 = vPos.xz * refraction_uv_scale;
    vec2 uv1 = uv0;
    vec2 uv2 = uv0;
    vec2 uv3 = uv0;

    mediump vec3 refracted_dir = refract(to_ocean, normal, 1.0 / 1.33);
    mediump float dir_to_bottom = -refracted_dir.y;

    if (dir_to_bottom <= 0.0)
    {
        refracted_dir = refract(to_ocean, vec3(0.0, 1.0, 0.0), 1.0 / 1.33);
        dir_to_bottom = -refracted_dir.y;
    }

    if (dir_to_bottom > 0.0)
    {
        mediump vec2 refracted_xz_offset;
        mediump vec4 depths = (refraction_depths + vPos.y) / dir_to_bottom;
        refracted_xz_offset = refracted_dir.xz * depths.x;
        uv0 = refraction_uv_scale * (refracted_xz_offset + vPos.xz);
        refracted_xz_offset = refracted_dir.xz * depths.y;
        uv1 = refraction_uv_scale * (refracted_xz_offset + vPos.xz);
        refracted_xz_offset = refracted_dir.xz * depths.z;
        uv2 = refraction_uv_scale * (refracted_xz_offset + vPos.xz);
        refracted_xz_offset = refracted_dir.xz * depths.w;
        uv3 = refraction_uv_scale * (refracted_xz_offset + vPos.xz);
    }

#if VARIANT_BIT_2
    mediump float turbulence_extent_mod = exp2(2.0 * turbulence);
    BandlimitedPixelInfo info;

    info = compute_pixel_weights(uv0, refraction_size.xy, refraction_size.zw, turbulence_extent_mod);
    mediump vec4 emissive0 = sample_bandlimited_pixel(uDirectRefraction0, uv0, info, 2.0 * turbulence);
    info = compute_pixel_weights(uv1, refraction_size.xy, refraction_size.zw, turbulence_extent_mod);
    mediump vec4 emissive1 = sample_bandlimited_pixel(uDirectRefraction1, uv1, info, 2.0 * turbulence);
    info = compute_pixel_weights(uv2, refraction_size.xy, refraction_size.zw, turbulence_extent_mod);
    mediump vec4 emissive2 = sample_bandlimited_pixel(uDirectRefraction2, uv2, info, 2.0 * turbulence);
    info = compute_pixel_weights(uv3, refraction_size.xy, refraction_size.zw, turbulence_extent_mod);
    mediump vec4 emissive3 = sample_bandlimited_pixel(uDirectRefraction3, uv3, info, 2.0 * turbulence);

    mediump vec3 emissive = refraction_emissive_mod * merge_emissive_layers(emissive0, emissive1, emissive2, emissive3);
#else
    mediump vec4 emissive0 = texture(uDirectRefraction0, uv0, 2.0 * turbulence);
    mediump vec4 emissive1 = texture(uDirectRefraction1, uv1, 2.0 * turbulence);
    mediump vec4 emissive2 = texture(uDirectRefraction2, uv2, 2.0 * turbulence);
    mediump vec4 emissive3 = texture(uDirectRefraction3, uv3, 2.0 * turbulence);
    mediump vec3 emissive = refraction_emissive_mod * merge_emissive_layers(emissive0, emissive1, emissive2, emissive3);
#endif
#else
    const mediump vec3 emissive = vec3(0.0);
#endif

    emit_render_target(emissive, vec4(base_color, 1.0), normal,
                       1.0 - 0.2 * turbulence, 0.2 * turbulence, 1.0, vPos);
}
