#version 310 es
precision mediump float;

layout(set = 0, binding = 0) uniform mediump samplerCube uReflection;
layout(set = 0, binding = 1) uniform mediump samplerCube uIrradiance;

layout(input_attachment_index = 0, set = 1, binding = 0) uniform mediump subpassInput BaseColor;
layout(input_attachment_index = 1, set = 1, binding = 1) uniform mediump subpassInput Normal;
layout(input_attachment_index = 2, set = 1, binding = 2) uniform mediump subpassInput PBR;
layout(input_attachment_index = 3, set = 1, binding = 3) uniform highp subpassInput Depth;
layout(location = 0) out vec3 FragColor;
layout(location = 0) in highp vec4 vClip;

layout(std430, push_constant) uniform Registers
{
    mat4 inverse_view_projection;
    vec3 direction;
    vec3 color;
    vec3 camera_pos;
} registers;

void main()
{
    highp float depth = subpassLoad(Depth).x;
    highp vec4 clip = vClip + depth * registers.inverse_view_projection[2];
    highp vec3 pos = clip.xyz / clip.w;

    vec3 normal = normalize(subpassLoad(Normal).xyz * 2.0 - 1.0);
    vec3 base_color = subpassLoad(BaseColor).rgb;

    vec3 lighting = 0.02 * base_color;

    // Diffuse direct light.
    float ndotl = clamp(dot(normal, registers.direction), 0.0, 1.0);
    lighting += ndotl * base_color;

    // Diffuse skydome lighting.
    vec3 irradiance = textureLod(uIrradiance, normal, 7.0).rgb;
    lighting += 0.1 * irradiance * base_color;

    vec3 eye_vec = registers.camera_pos - pos;
    vec3 eye_dir = normalize(eye_vec);

    float reflect_orient = clamp(dot(eye_dir, normal), 0.0, 1.0);
    float fresnel = 0.04 + 0.96 * pow((1.0 - reflect_orient), 5.0);

    vec3 reflected = reflect(-eye_dir, normal);
    vec3 skydome_reflected = texture(uReflection, reflected, 8.0).rgb;

    if (reflect_orient > 0.0)
        lighting += 0.1 * skydome_reflected * fresnel;

    FragColor = lighting;
}