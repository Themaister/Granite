#ifndef RENDER_TARGET_H_
#define RENDER_TARGET_H_

#if !defined(RENDERER_DEFERRED) && !defined(RENDERER_FORWARD)
#define RENDERER_DEFERRED
#endif

#if defined(RENDERER_DEFERRED)
layout(location = 0) out vec3 Emissive;
layout(location = 1) out vec4 BaseColor;
layout(location = 2) out vec3 Normal;
layout(location = 3) out vec2 PBR;

void emit_render_target(vec3 emissive, vec4 base_color, vec3 normal, float metallic, float roughness, float ambient, vec3 eye_dir)
{
    Emissive = emissive;
    BaseColor = vec4(base_color.rgb, ambient);
    Normal = 0.5 * normal + 0.5;
    PBR = vec2(metallic, roughness);
}
#elif defined(RENDERER_FORWARD)
layout(location = 0) out vec4 Color;
#include "../lights/lighting.h"
#include "../lights/fog.h"

void emit_render_target(vec3 emissive, vec4 base_color, vec3 normal, float metallic, float roughness, float ambient, vec3 eye_dir)
{
    vec3 pos = eye_dir + global.camera_position;

#ifdef SHADOWS
#if SHADOW_CASCADES
    vec4 clip_shadow_near = shadow.near * vec4(pos, 1.0);
#else
    vec4 clip_shadow_near = vec4(0.0);
#endif
    vec4 clip_shadow_far = shadow.far * vec4(pos, 1.0);
#endif

    vec3 lighting = emissive + compute_lighting(
        MaterialProperties(base_color, normal, metallic, roughness, ambient),
        LightInfo(pos, global.camera_position, global.camera_front, directional.direction, directional.color
#ifdef SHADOWS
                , clip_shadow_near, clip_shadow_far, shadow.inv_cutoff_distance
#endif
        )
#ifdef ENVIRONMENT
        , EnvironmentInfo(environment.intensity, environment.mipscale)
#endif
        );

#ifdef FOG
    lighting = apply_fog(lighting, eye_dir, fog.color, fog.falloff);
#endif

    Color = lighting;
}
#endif

#endif