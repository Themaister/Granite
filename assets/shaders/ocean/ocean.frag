#version 450

#include "../inc/render_target.h"

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec4 vGradNormalUV;

layout(set = 2, binding = 2) uniform mediump sampler2D uGradJacobian;
layout(set = 2, binding = 3) uniform mediump sampler2D uNormal;

void main()
{
    mediump vec3 grad_jacobian = texture(uGradJacobian, vGradNormalUV.xy).xyz;
    mediump vec2 N = 0.3 * texture(uNormal, vGradNormalUV.zw).xy;
    mediump float jacobian = grad_jacobian.z;
    mediump float turbulence = max(2.0 - jacobian + dot(abs(N), vec2(1.2)), 0.0);

    N += grad_jacobian.xy;
    mediump vec3 normal = normalize(vec3(-N.x, 1.0, -N.y));

    const vec3 base_color = vec3(0.4, 0.6, 0.8);
    emit_render_target(vec3(0.0), vec4(base_color, 1.0), normal, 1.0, 0.2 * turbulence, 1.0, vPos);
}