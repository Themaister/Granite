#version 310 es
precision mediump float;

layout(location = 0) in highp vec2 vUV;
layout(location = 1) in mediump vec3 vEyeVec;

#include "inc/render_target.h"

#if defined(HAVE_EMISSIVE_REFLECTION) && HAVE_EMISSIVE_REFLECTION
layout(set = 2, binding = 0) uniform sampler2D uPlaneReflection;
#endif
#if defined(HAVE_EMISSIVE_REFRACTION) && HAVE_EMISSIVE_REFRACTION
layout(set = 2, binding = 1) uniform sampler2D uPlaneRefraction;
#endif
layout(set = 2, binding = 2) uniform sampler2D uNormal;

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
    vec3 tangent = texture(uNormal, registers.normal_offset_scale.zw * vUV + registers.normal_offset_scale.xy).xyz * 2.0 - 1.0;
    vec3 normal = normalize(registers.normal * tangent.z + registers.tangent * tangent.x + registers.bitangent * tangent.y);

    vec2 uv_offset = tangent.xy * 0.01;

    vec3 emissive = registers.base_emissive;
#if defined(HAVE_EMISSIVE_REFLECTION) && HAVE_EMISSIVE_REFLECTION
    vec2 reflection_uv = vUV + uv_offset;
    vec3 reflection = texture(uPlaneReflection, reflection_uv, 1.0).rgb;
    float NoV = abs(clamp(dot(normal, normalize(vEyeVec)), -1.0, 1.0));
    float reflection_coeff = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);
    emissive += reflection * reflection_coeff;
#endif

#if defined(HAVE_EMISSIVE_REFRACTION) && HAVE_EMISSIVE_REFRACTION
    vec2 refraction_uv = vec2(1.0 - vUV.x - tangent.x * 0.02, vUV.y - tangent.y * 0.02);
    vec3 refraction = texture(uPlaneRefraction, refraction_uv).rgb;

    // Even at 90 degrees transmission angle, incident angle is maximum 48 deg. With schlick approx, the reflection
    // coeff is at most 0.025, completely insignificant, so just always transmit the refracted pixels.
    float refraction_coeff = 0.98;
    emissive += refraction * refraction_coeff;
#endif

    emit_render_target(emissive, vec4(0.02, 0.02, 0.02, 1.0), normal, 1.0, 0.1, 1.0, vEyeVec);
}
