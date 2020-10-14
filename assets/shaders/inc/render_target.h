#ifndef RENDER_TARGET_H_
#define RENDER_TARGET_H_

#if !defined(RENDERER_DEFERRED) && !defined(RENDERER_FORWARD)
#define RENDERER_DEFERRED
#endif

#if defined(RENDERER_DEFERRED)
#if defined(HAVE_EMISSIVE) && HAVE_EMISSIVE
layout(location = 0) out mediump vec3 Emissive;
#endif
layout(location = 1) out mediump vec4 BaseColor;
layout(location = 2) out mediump vec3 Normal;
layout(location = 3) out mediump vec2 PBR;

void emit_render_target(mediump vec3 emissive, mediump vec4 base_color, mediump vec3 normal,
        mediump float metallic, mediump float roughness,
        mediump float ambient, vec3 pos)
{
#if defined(HAVE_EMISSIVE) && HAVE_EMISSIVE
    Emissive = emissive;
#endif
    BaseColor = vec4(base_color.rgb, ambient);
    Normal = 0.5 * normal + 0.5;
    PBR = vec2(metallic, roughness);
}
#elif defined(RENDERER_FORWARD)
layout(location = 0) out mediump vec4 Color;
#include "render_parameters.h"
#include "../lights/lighting.h"
#include "../lights/fog.h"
#include "../lights/volumetric_fog.h"

void emit_render_target(mediump vec3 emissive, mediump vec4 base_color, mediump vec3 normal,
        mediump float metallic, mediump float roughness, mediump float ambient, vec3 pos)
{
#ifdef AMBIENT_OCCLUSION
    ambient *= textureLod(uAmbientOcclusion, gl_FragCoord.xy * resolution.inv_resolution, 0.0).x;
#endif

    mediump vec3 lighting = emissive + compute_lighting(
        base_color.rgb, normal, metallic, roughness, ambient, base_color.a,
        pos, global.camera_position, global.camera_front, directional.direction, directional.color
#ifdef ENVIRONMENT
        , environment.intensity, environment.mipscale
#endif
	);

#if defined(VOLUMETRIC_FOG)
    mediump vec4 fog = sample_volumetric_fog(uFogVolume,
        gl_FragCoord.xy * resolution.inv_resolution,
        dot(pos - global.camera_position, global.camera_front),
        volumetric_fog.slice_z_log2_scale);
    lighting = fog.rgb + lighting * fog.a;
#elif defined(FOG)
    lighting = apply_fog(lighting, pos - global.camera_position, fog.color, fog.falloff);
#endif

#ifdef REFRACTION
	vec4 pos_near_clip = global.inv_view_projection * vec4(2.0 * gl_FragCoord.xy * resolution.inv_resolution - 1.0, 0.0, 1.0);
	vec3 pos_near = pos_near_clip.xyz / pos_near_clip.w;
    float distance = distance(pos, pos_near);
    lighting *= exp2(-refraction.falloff * distance);
#endif

    Color = vec4(lighting, base_color.a);
}
#endif

#endif
