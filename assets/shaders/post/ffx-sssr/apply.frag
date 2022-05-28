#version 450

#include "sssr_util.h"
#include "../../lights/pbr.h"

layout(location = 0) out vec3 FragColor;
layout(location = 0) in vec2 vUV;

layout(input_attachment_index = 0, set = 0, binding = 1) uniform mediump subpassInput uBaseColor;
layout(input_attachment_index = 1, set = 0, binding = 2) uniform mediump subpassInput uNormal;
layout(input_attachment_index = 2, set = 0, binding = 3) uniform mediump subpassInput uPBR;
layout(input_attachment_index = 3, set = 0, binding = 4) uniform subpassInput uDepth;
layout(set = 0, binding = 0) uniform sampler2D uReflected;
layout(set = 0, binding = 5) uniform sampler2D uBRDFLut;

void main()
{
    mediump vec2 mr = subpassLoad(uPBR).xy;
    mediump float metallic = mr.x;
    mediump float roughness = mr.y;
    float clip_depth = subpassLoad(uDepth).x;

    vec2 clip_uv = vUV * 2.0 - 1.0;
    vec3 world_pos = FFX_SSSR_ScreenSpaceToWorldSpace(vec3(clip_uv, clip_depth));
    mediump vec3 V = normalize(sssr.camera_position - world_pos);
    mediump vec3 N = normalize(subpassLoad(uNormal).xyz * 2.0 - 1.0);

    // Modulate by IBL parameters.
    mediump float NoV = clamp(dot(N, V), 0.0, 1.0);
    mediump vec3 F0 = compute_F0(subpassLoad(uBaseColor).rgb, metallic);
    mediump vec3 F = fresnel_ibl(F0, NoV, roughness);
    mediump vec2 brdf = textureLod(uBRDFLut, vec2(NoV, roughness), 0.0).xy;
    FragColor = textureLod(uReflected, vUV, 0.0).rgb * (F * brdf.x + brdf.y);
}