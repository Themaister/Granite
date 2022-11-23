#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(location = 0) out vec4 FragColor;

layout(set = 1, binding = 0) uniform Config
{
    mat4 primary_conversion;
    float hdr_pre_exposure;
    float ui_pre_exposure;
    float max_light_level;
    float inv_max_light_level;
} config;

layout(set = 0, binding = 0) uniform texture2D uHDR;
layout(set = 0, binding = 1) uniform texture2D uUI;

vec3 encode_pq(vec3 nits)
{
    // PQ
    vec3 y = nits / 10000.0;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    vec3 num = c1 + c2 * pow(y, vec3(m1));
    vec3 den = 1.0 + c3 * pow(y, vec3(m1));
    vec3 n = pow(num / den, vec3(m2));
    return n;
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec3 hdr = texelFetch(uHDR, coord, 0).rgb;
    vec4 ui = texelFetch(uUI, coord, 0);
    vec3 col = hdr * (config.hdr_pre_exposure * ui.a) + ui.rgb * config.ui_pre_exposure;

    // Convert to ST.2020 primaries.
    col = mat3(config.primary_conversion) * col;

    col *= config.inv_max_light_level;

    const float K = 4.0;
    vec3 col_k = col * K;
    vec3 saturated = col_k / (1.0 + col_k);

    col = mix(col, saturated, greaterThan(col, vec3(0.75)));
    vec3 pq = encode_pq(col * config.max_light_level);
    FragColor = vec4(pq, 1.0);
}
