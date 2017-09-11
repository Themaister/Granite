#ifndef PBR_H_
#define PBR_H_

#ifndef PI
#define PI 3.1415628
#endif

float D_GGX(float roughness, float NoH)
{
    float m = roughness * roughness;
    float m2 = m * m;
    float d = (NoH * m2 - NoH) * NoH + 1.0;
    return m2 / (PI * d * d);
}

float G_schlick(float roughness, float NoV, float NoL)
{
    float r = roughness + 1.0;
    float k = r * r * (1.0 / 8.0);
    float V = NoV * (1.0 - k) + k;
    float L = NoL * (1.0 - k) + k;
    return 0.25 / (V * L); // 1 / (4 * NoV * NoL) is folded in here.
}

vec3 blinn_specular(float NoH, vec3 specular, float roughness)
{
	float k = 1.999 / (roughness * roughness);
	return min(1.0, 3.0 * 0.0398 * k) * pow(NoH, k) * specular;
}

vec3 cook_torrance_specular(float NoL, float NoV, float NoH, vec3 specular, float roughness)
{
    float D = D_GGX(roughness, NoH);
    float G = G_schlick(roughness, NoV, NoL);
    return specular * G * D;
}

// Just something hacky.
vec2 image_based_brdf(float roughness, float NoV)
{
    return vec2(1.0 - sqrt(roughness), 0.0);
}

vec3 fresnel(vec3 F0, float HoV)
{
	return mix(F0, vec3(1.0), pow((1.0 - HoV), 5.0));
}

vec3 fresnel_ibl(vec3 F0, float cos_theta, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cos_theta, 5.0);
}

vec3 compute_F0(vec3 base_color, float metallic)
{
	return mix(vec3(0.04), base_color, metallic);
}

#endif
