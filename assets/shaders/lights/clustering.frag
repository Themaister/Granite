#version 450

#include "../inc/helper_invocation.h"
#include "../inc/global_bindings.h"
#include "clusterer.h"

layout(input_attachment_index = 0, set = 3, binding = 0) uniform mediump subpassInput BaseColor;
layout(input_attachment_index = 1, set = 3, binding = 1) uniform mediump subpassInput Normal;
layout(input_attachment_index = 2, set = 3, binding = 2) uniform mediump subpassInput PBR;
layout(input_attachment_index = 3, set = 3, binding = 3) uniform subpassInput Depth;

layout(location = 0) out mediump vec3 FragColor;
layout(location = 0) in vec4 vClip;

layout(std430, push_constant) uniform Registers
{
    vec4 inverse_view_projection_col2;
    vec3 camera_pos;
    vec2 inv_resolution;
} registers;

#ifdef AMBIENT_OCCLUSION
layout(set = 0, binding = BINDING_GLOBAL_AMBIENT_OCCLUSION) uniform mediump sampler2D uAmbientOcclusion;
#endif

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

    FragColor = compute_cluster_light(
		base_color_ambient.rgb, N, mr.x, mr.y,
		pos, registers.camera_pos
#ifdef CLUSTERER_BINDLESS
        , registers.inv_resolution
#endif
    );

#ifdef VOLUMETRIC_DIFFUSE
#ifdef AMBIENT_OCCLUSION
    mediump float ambient_term =
            textureLod(uAmbientOcclusion, gl_FragCoord.xy * registers.inv_resolution, 0.0).x;
#else
    const mediump float ambient_term = 1.0;
#endif
    FragColor += base_color_ambient.rgb *
            ((1.0 - mr.x) * base_color_ambient.a * ambient_term) *
            compute_volumetric_diffuse(pos, N);
#endif
}
