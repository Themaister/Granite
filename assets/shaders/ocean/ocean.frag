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
layout(set = 2, binding = 4) uniform mediump sampler2D uDirectRefraction;

layout(set = 2, binding = 5, std140) uniform Refraction
{
    vec4 refraction_size;
    float refraction_uv_scale;
    float refraction_depth;
    float refraction_emissive_mod;
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
    vec2 uv = vPos.xz * refraction_uv_scale;
    mediump vec3 emissive_mod = vec3(0.0);

    mediump vec3 refracted_dir = refract(to_ocean, normal, 1.0 / 1.33);
    mediump float dir_to_bottom = -refracted_dir.y;

    if (dir_to_bottom <= 0.0)
    {
        refracted_dir = refract(to_ocean, vec3(0.0, 1.0, 0.0), 1.0 / 1.33);
        dir_to_bottom = -refracted_dir.y;
    }

    if (dir_to_bottom > 0.0)
    {
        mediump vec2 refracted_xz_offset = refracted_dir.xz * ((refraction_depth + vPos.y) / dir_to_bottom);
        mediump vec3 refracted_pos_offset = vec3(refracted_xz_offset.x, vPos.y + refraction_depth, refracted_xz_offset.y);
        mediump float fade_length = length(refracted_pos_offset);
        uv = refraction_uv_scale * (refracted_xz_offset + vPos.xz);
        emissive_mod = refraction_emissive_mod * exp2(-vec3(0.4, 0.3, 0.2) * fade_length);
    }
#if VARIANT_BIT_2
    BandlimitedPixelInfo info =
        compute_pixel_weights(uv, refraction_size.xy, refraction_size.zw, exp2(3.0 * turbulence));
    mediump vec3 emissive = emissive_mod * sample_bandlimited_pixel(uDirectRefraction, uv, info, 3.0 * turbulence).rgb;
#else
    mediump vec3 emissive = emissive_mod * texture(uDirectRefraction, uv, 3.0 * turbulence).rgb;
#endif
#else
    const mediump vec3 emissive = vec3(0.0);
#endif

    emit_render_target(emissive, vec4(base_color, 1.0), normal,
                       1.0 - 0.2 * turbulence, 0.2 * turbulence, 1.0, vPos);
}
