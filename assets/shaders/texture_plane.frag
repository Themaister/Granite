#version 450
precision highp float;
precision highp int;

layout(location = 0) in highp vec2 vUV;
layout(location = 1) in highp vec3 vPos;

#include "inc/render_target.h"
#include "inc/render_parameters.h"
#include "inc/two_component_normal.h"

#if defined(HAVE_EMISSIVE_REFLECTION) && HAVE_EMISSIVE_REFLECTION
layout(set = 2, binding = 0) uniform mediump sampler2D uPlaneReflection;
#endif
#if defined(HAVE_EMISSIVE_REFRACTION) && HAVE_EMISSIVE_REFRACTION
layout(set = 2, binding = 1) uniform mediump sampler2D uPlaneRefraction;
#endif
layout(set = 2, binding = 2) uniform mediump sampler2D uNormal;

layout(std430, push_constant) uniform Registers
{
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;

    vec3 position;
    vec3 dPdx;
    vec3 dPdy;
    vec4 normal_offset_scale;
    vec3 base_emissive;
} registers;

void main()
{
    mediump vec3 tangent = two_component_normal(texture(uNormal, registers.normal_offset_scale.zw * vUV + registers.normal_offset_scale.xy).xy * 2.0 - 1.0);
    mediump vec3 normal = normalize(registers.normal * tangent.z + registers.tangent * tangent.x + registers.bitangent * tangent.y);

    vec2 uv_offset = tangent.xy * 0.01;

    mediump vec3 emissive = registers.base_emissive;
#if defined(HAVE_EMISSIVE_REFLECTION) && HAVE_EMISSIVE_REFLECTION
    vec2 reflection_uv = vUV + uv_offset;
    mediump vec3 reflection = texture(uPlaneReflection, reflection_uv, 1.0).rgb;
    mediump float NoV = abs(clamp(dot(normal, normalize(vPos - global.camera_position)), -1.0, 1.0));
    mediump float reflection_coeff = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);
    emissive += reflection * reflection_coeff;
#endif

#if defined(HAVE_EMISSIVE_REFRACTION) && HAVE_EMISSIVE_REFRACTION
    vec2 refraction_uv = vec2(1.0 - vUV.x - tangent.x * 0.02, vUV.y - tangent.y * 0.02);
    mediump vec3 refraction = texture(uPlaneRefraction, refraction_uv).rgb;

    // Even at 90 degrees transmission angle, incident angle is maximum 48 deg. With schlick approx, the reflection
    // coeff is at most 0.025, completely insignificant, so just always transmit the refracted pixels.
    mediump float refraction_coeff = 0.98;
    emissive += refraction * refraction_coeff;
#endif

    emit_render_target(emissive, vec4(0.02, 0.02, 0.02, 1.0), normal, 1.0, 0.0, 1.0, vPos);
}
