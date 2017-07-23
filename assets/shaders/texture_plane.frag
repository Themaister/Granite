#version 310 es
precision mediump float;

layout(location = 0) in highp vec2 vUV;
layout(location = 1) in mediump vec3 vEyeVec;

layout(location = 0) out vec3 Emissive;
layout(location = 1) out vec4 BaseColor;
layout(location = 2) out vec3 Normal;
layout(location = 3) out vec2 PBR;

layout(set = 2, binding = 0) uniform sampler2D uReflection;
layout(set = 2, binding = 1) uniform sampler2D uRefraction;
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
} registers;

void main()
{
    vec3 tangent = texture(uNormal, registers.normal_offset_scale.zw * vUV + registers.normal_offset_scale.xy).xyz * 2.0 - 1.0;
    vec3 normal = normalize(registers.normal * tangent.z + registers.tangent * tangent.x + registers.bitangent * tangent.y);

    vec2 uv_offset = tangent.xy * 0.01;

    vec2 reflection_uv = vUV + uv_offset;
    vec2 refraction_uv = vec2(1.0 - vUV.x - tangent.x * 0.02, vUV.y - tangent.y * 0.02);
    vec3 refraction = texture(uRefraction, refraction_uv).rgb;
    vec3 reflection = texture(uReflection, reflection_uv, 1.0).rgb;

    float NoV = abs(clamp(dot(normal, normalize(vEyeVec)), -1.0, 1.0));
    float reflection_coeff = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);

    // Even at 90 degrees transmission angle, incident angle is maximum 48 deg. With schlick approx, the reflection
    // coeff is at most 0.025, completely insignificant, so just always transmit the refracted pixels.
    float refraction_coeff = 0.98;

    // Add in an ambient emissive color which is very dark blue-ish.
    Emissive = refraction * refraction_coeff + reflection * reflection_coeff + vec3(0.02, 0.025, 0.04);

    Normal = normal * 0.5 + 0.5;
    BaseColor = vec4(0.02, 0.02, 0.02, 1.0);
    PBR = vec2(1.0, 0.0); // No diffuse, no specular, only reflection.
}
