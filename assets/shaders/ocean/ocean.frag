#version 450
precision highp float;
precision highp int;

#if defined(RENDERER_FORWARD)
#include "../inc/subgroup_extensions.h"
#endif
#include "../inc/render_target.h"

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec4 vGradNormalUV;

layout(set = 2, binding = 2) uniform mediump sampler2D uGradJacobian;
layout(set = 2, binding = 3) uniform mediump sampler2D uNormal;

void main()
{
    mediump vec3 grad_jacobian = texture(uGradJacobian, vGradNormalUV.xy).xyz;
    mediump vec2 N = texture(uNormal, vGradNormalUV.zw).xy;
    mediump float jacobian = grad_jacobian.z;
    mediump float turbulence = max(2.0 - jacobian + dot(abs(N), vec2(0.4)), 0.0);

    N += grad_jacobian.xy;
    mediump vec3 normal = normalize(vec3(-N.x, 1.0, -N.y));
    const vec3 base_color = vec3(1.0);
    const mediump vec3 emissive = vec3(0.0);

    emit_render_target(emissive, vec4(base_color, 1.0), normal,
                       1.0 - 0.05 * turbulence, 0.03 * turbulence, 1.0, vPos);
}
