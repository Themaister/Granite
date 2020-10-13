#version 450
precision highp float;
precision highp int;

#include "../inc/subgroup_extensions.h"

#define SHADOW_NUM_CASCADES 4
layout(std140, set = 0, binding = 0) uniform Transforms
{
    mat4 inverse_view_projection;
    mat4 transforms[SHADOW_NUM_CASCADES];
} shadows;

layout(std430, push_constant) uniform Registers
{
    vec4 inverse_view_projection_col2;
    vec3 color;
    float environment_intensity;
    vec3 camera_pos;
    float environment_mipscale;
    vec3 direction;
    float cascade_log_bias;
    vec3 camera_front;
    vec2 inv_resolution;
} registers;

#define SHADOW_TRANSFORMS shadows.transforms
#define SHADOW_CASCADE_LOG_BIAS registers.cascade_log_bias
#include "lighting.h"

layout(input_attachment_index = 0, set = 3, binding = 0) uniform mediump subpassInput BaseColor;
layout(input_attachment_index = 1, set = 3, binding = 1) uniform mediump subpassInput Normal;
layout(input_attachment_index = 2, set = 3, binding = 2) uniform mediump subpassInput PBR;
layout(input_attachment_index = 3, set = 3, binding = 3) uniform subpassInput Depth;

layout(location = 0) out mediump vec3 FragColor;
layout(location = 0) in vec4 vClip;

//#undef AMBIENT_OCCLUSION

void main()
{
    // Load material information.
    float depth = subpassLoad(Depth).x;
    mediump vec2 mr = subpassLoad(PBR).xy;
    mediump vec4 base_color_ambient = subpassLoad(BaseColor);
    mediump vec3 N = subpassLoad(Normal).xyz * 2.0 - 1.0;

    // Reconstruct positions.
    vec4 clip = vClip + depth * registers.inverse_view_projection_col2;
    vec3 pos = clip.xyz / clip.w;

#ifdef AMBIENT_OCCLUSION
    mediump float base_ambient = textureLod(uAmbientOcclusion, gl_FragCoord.xy * registers.inv_resolution, 0.0).x;
#else
    const mediump float base_ambient = 1.0;
#endif

    FragColor = compute_lighting(
		base_color_ambient.rgb, N, mr.x, mr.y, base_color_ambient.a * base_ambient, 1.0,
		pos, registers.camera_pos, registers.camera_front, registers.direction, registers.color
#ifdef ENVIRONMENT
		, registers.environment_intensity, registers.environment_mipscale
#endif
    );
}
