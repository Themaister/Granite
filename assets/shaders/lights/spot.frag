#version 450
precision highp float;
precision highp int;

#define POSITIONAL_LIGHT_DEFERRED

#if defined(VARIANT_BIT_2)
#define INSTANCING
#define POSITIONAL_LIGHT_INSTANCING
#endif

#if defined(VARIANT_BIT_1)
#define POSITIONAL_LIGHTS_SHADOW
#endif

#if defined(VARIANT_BIT_3)
#define POSITIONAL_SHADOW_VSM
#endif

#define SPOT_LIGHT_EARLY_OUT
#include "spot.h"

layout(location = 0) out mediump vec3 FragColor;

#ifdef INSTANCING
layout(location = 0) flat in int vIndex;
#endif

layout(input_attachment_index = 0, set = 3, binding = 0) uniform mediump subpassInput BaseColor;
layout(input_attachment_index = 1, set = 3, binding = 1) uniform mediump subpassInput Normal;
layout(input_attachment_index = 2, set = 3, binding = 2) uniform mediump subpassInput PBR;
layout(input_attachment_index = 3, set = 3, binding = 3) uniform subpassInput Depth;

layout(std430, push_constant) uniform Registers
{
    mat4 inverse_view_projection;
    vec3 camera_pos;
    vec2 inv_resolution;
} registers;

//#define SPOT_DEBUG

void main()
{
    // Load material information.
    float depth = subpassLoad(Depth).x;
    mediump vec2 mr = subpassLoad(PBR).xy;
    mediump vec4 base_color_ambient = subpassLoad(BaseColor);
    mediump vec3 N = subpassLoad(Normal).xyz * 2.0 - 1.0;

    // Reconstruct positions.
    vec4 clip = registers.inverse_view_projection * vec4(2.0 * gl_FragCoord.xy * registers.inv_resolution - 1.0, depth, 1.0);
    vec3 pos = clip.xyz / clip.w;

#ifdef SPOT_DEBUG
    FragColor = vec3(0.1, 0.0, 0.0);
    return;
#endif

    FragColor = compute_spot_light(
#ifdef INSTANCING
        vIndex,
#else
        0,
#endif
        base_color_ambient.rgb, N, mr.x, mr.y, pos, registers.camera_pos);
}
