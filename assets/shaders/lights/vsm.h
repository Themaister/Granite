#ifndef VSM_H_
#define VSM_H_

#ifdef CLUSTERER_BINDLESS
#include "linear_clamp_sampler.h"
#endif

mediump float vsm(float depth, vec2 moments)
{
    mediump float shadow_term = 1.0;
    if (depth > moments.x)
    {
        float variance = max(moments.y - moments.x * moments.x, 0.00001);
        float d = depth - moments.x;
        shadow_term = variance / (variance + d * d);
        shadow_term = clamp((shadow_term - 0.25) / 0.75, 0.0, 1.0); // Avoid some lighting leaking.
    }
    return shadow_term;
}

#endif