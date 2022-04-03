#version 450
layout(location = 0) in vec2 Position;
layout(location = 0) out highp vec2 vUV;

#define A_GLSL 1
#define A_GPU 1
#include "../ffx-a/ffx_a.h"
#include "ffx_fsr1.h"

#include "../../inc/prerotate.h"

layout(push_constant) uniform Registers
{
	vec2 out_resolution;
};

void main()
{
    gl_Position = vec4(Position, 0.0, 1.0);
    vUV = (0.5 * Position + 0.5) * out_resolution;
    prerotate_fixup_clip_xy();
}
