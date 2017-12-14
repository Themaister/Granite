#version 450
precision highp float;
precision highp int;

layout(set = 0, binding = 0) uniform mediump sampler2D uInput;
#if HISTORY
layout(set = 0, binding = 1) uniform mediump sampler2D uHistoryInput;
layout(set = 0, binding = 2) uniform sampler2D uDepth;
#endif
layout(location = 0) out mediump vec3 FragColor;
layout(location = 1) out mediump vec3 SaveFragColor;
layout(location = 0) in vec2 vUV;

layout(push_constant, std430) uniform Registers
{
    mat4 reproj;
    vec2 offset_lo;
    vec2 offset_mid;
    vec2 offset_hi;
    vec2 offset_next;
} registers;

void main()
{
    const mediump float sharpen = 0.25;
    mediump vec3 sharpened_input =
        (1.0 + 2.0 * sharpen) * textureLod(uInput, vUV + registers.offset_mid, 0.0).rgb -
        sharpen * textureLod(uInput, vUV + registers.offset_lo, 0.0).rgb -
        sharpen * textureLod(uInput, vUV + registers.offset_hi, 0.0).rgb;
    sharpened_input = clamp(sharpened_input, 0.0, 1.0);

#if HISTORY
    float min_depth = min(textureLod(uDepth, vUV, 0.0).x, textureLod(uDepth, vUV + registers.offset_next, 0.0).x);
    vec4 clip = vec4(2.0 * vUV - 1.0, min_depth, 1.0);
    vec4 reproj_pos = registers.reproj * clip;
    mediump vec3 history_color = textureProjLod(uHistoryInput, reproj_pos.xyw, 0.0).rgb;
    const mediump float lerp_factor = 0.5;
    mediump vec3 color = mix(history_color, sharpened_input, lerp_factor);
#else
    mediump vec3 color = sharpened_input;
#endif

    FragColor = color;
    SaveFragColor = sharpened_input;
}