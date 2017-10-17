#version 450
precision highp int;
precision highp float;

layout(set = 2, binding = 0) uniform samplerCube uCube;
layout(location = 0) out vec4 FragColor;
layout(location = 0) in highp vec3 vDirection;

layout(push_constant, std430) uniform Registers
{
    float lod;
    float roughness;
} registers;

#define PI 3.1415628

// Shamelessly copypasted from learnopengl.com

float RadicalInverse_VdC(uint bits)
{
    bits = bitfieldReverse(bits);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 Hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    // from spherical coordinates to cartesian coordinates
    vec3 H;
    H.x = cos(phi) * sin_theta;
    H.y = sin(phi) * sin_theta;
    H.z = cos_theta;

    // from tangent-space vector to world-space sample vector
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sample_vec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sample_vec);
}

void main()
{
    vec3 N = normalize(vDirection);
    vec3 R = N;
    vec3 V = R;

    const uint SAMPLE_COUNT = 1024u;
    float total_weight = 0.0;
    vec3 prefiltered_color = vec3(0.0);
    for (uint i = 0u; i < SAMPLE_COUNT; i++)
    {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H  = ImportanceSampleGGX(Xi, N, registers.roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            prefiltered_color += textureLod(uCube, L, registers.lod).rgb * NdotL;
            total_weight      += NdotL;
        }
    }
    prefiltered_color = prefiltered_color / total_weight;
    FragColor = vec4(prefiltered_color, 1.0);
}