#version 450

#include "../inc/pbr.h"

layout(set = 0, binding = 1) uniform samplerCube uReflection;
layout(set = 0, binding = 2) uniform samplerCube uIrradiance;
layout(set = 0, binding = 3) uniform sampler2D uShadowmap;

layout(input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput BaseColor;
layout(input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput Normal;
layout(input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput PBR;
layout(input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput Depth;
layout(location = 0) out vec3 FragColor;
layout(location = 0) in vec4 vClip;
layout(location = 1) in vec4 vShadowClip;

layout(std430, push_constant) uniform Registers
{
    vec4 inverse_view_projection_col2;
    vec4 shadow_projection_col2;
    vec3 direction;
    vec3 color;
	float environment_intensity;
    vec3 camera_pos;
	float environment_mipscale;
} registers;

float sample_vsm(vec4 clip_shadow)
{
    vec3 coord = clip_shadow.xyz / clip_shadow.w;
    vec2 moments = texture(uShadowmap, coord.xy).xy;

    float shadow_term = 1.0f;
    if (coord.z > moments.x)
    {
        float variance = max(moments.y - moments.x * moments.x, 0.001);
        float d = coord.z - moments.x;
        shadow_term = variance / (variance + d * d);
    }
    return shadow_term;
}

float sample_esm(vec4 clip_shadow)
{
    vec3 coord = clip_shadow.xyz / clip_shadow.w;
    float moment = texture(uShadowmap, coord.xy).x;
	float value = moment * exp2(-100.0 * coord.z);
	value *= value;
	value *= value;
	return clamp(value, 0.0, 1.0);
}

void main()
{
    // Load material information.
    float depth = subpassLoad(Depth).x;
    vec2 mr = subpassLoad(PBR).xy;
    float metallic = mr.x;
    float roughness = mr.y * 0.75 + 0.25;
    vec3 base_color = subpassLoad(BaseColor).rgb;
    vec3 N = subpassLoad(Normal).xyz * 2.0 - 1.0;

    // Reconstruct position.
    vec4 clip = vClip + depth * registers.inverse_view_projection_col2;

    // Sample shadowmap.
    vec4 clip_shadow = vShadowClip + depth * registers.shadow_projection_col2;
    float shadow_term = sample_esm(clip_shadow);

    vec3 pos = clip.xyz / clip.w;

    // Compute directional light.
    vec3 L = registers.direction;
    vec3 V = normalize(registers.camera_pos - pos);
    vec3 H = normalize(V + L);

    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float NoV = clamp(dot(N, V), 0.001, 1.0);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);
    float LoV = clamp(dot(L, V), 0.001, 1.0);

    vec3 F0 = compute_F0(base_color, metallic);

    vec3 specular_fresnel = fresnel(F0, HoV);
    vec3 specref = blinn_specular(NoH, specular_fresnel, roughness);

    specref *= NoL * shadow_term;
    vec3 diffref = NoL * shadow_term * (1.0 - specular_fresnel) * (1.0 / PI);

    // IBL diffuse term.
    //vec3 envdiff = registers.environment_intensity * textureLod(uIrradiance, N, 10.0).rgb * (1.0 / PI);
	vec3 envdiff = mix(vec3(0.2, 0.2, 0.2) / PI, vec3(0.2, 0.2, 0.3) / PI, clamp(N.y, 0.0, 1.0));

    // IBL specular term.
    vec3 reflected = reflect(-V, N);
    float minimum_lod = textureQueryLod(uReflection, reflected).y;
    vec3 envspec = registers.environment_intensity * textureLod(uReflection, reflected, max(roughness * registers.environment_mipscale, minimum_lod)).rgb;

    // Lookup reflectance terms.
    //vec2 brdf = textureLod(uBRDF, vec2(mr.y, 1.0 - NoV), 0.0).xy;
    vec2 brdf = image_based_brdf(roughness, NoV);

    vec3 iblspec = min(vec3(1.0), fresnel(F0, NoV) * brdf.x + brdf.y);
    envspec *= iblspec;

    vec3 reflected_light = specref + envspec;
    vec3 diffuse_light = (diffref + envdiff) * base_color * (1.0 - metallic);
    vec3 lighting = registers.color * (reflected_light + diffuse_light);

    FragColor = lighting;
}
