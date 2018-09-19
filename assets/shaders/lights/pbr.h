#ifndef PBR_H_
#define PBR_H_

#ifndef PI
#define PI 3.1415628
#endif

mediump float D_GGX(mediump float roughness, mediump vec3 N, mediump vec3 H)
{
#if 1
    float NoH = clamp(dot(N, H), 0.0001, 1.0);
    float m = roughness * roughness;
    float m2 = m * m;
    float d = (NoH * m2 - NoH) * NoH + 1.0;
    return m2 / (PI * d * d);
#else
	mediump float NoH = clamp(dot(N, H), 0.001, 1.0);
	mediump vec3 NxH = cross(N, H);
	mediump float one_minus_NoH_squared = min(dot(NxH, NxH), 1.0);
	mediump float linear_roughness = roughness * roughness;
	mediump float a = NoH * linear_roughness;
	mediump float k = linear_roughness / max(one_minus_NoH_squared + a * a, 0.0001);
	mediump float d = k * k * (1.0 / PI);
	return d;
#endif
}

mediump float G_schlick(mediump float roughness, mediump float NoV, mediump float NoL)
{
    mediump float r = roughness + 1.0;
    mediump float k = r * r * (1.0 / 8.0);
    mediump float V = NoV * (1.0 - k) + k;
    mediump float L = NoL * (1.0 - k) + k;
    return 0.25 / max(V * L, 0.001); // 1 / (4 * NoV * NoL) is folded in here.
}

mediump vec3 cook_torrance_specular(mediump vec3 N, mediump vec3 H, mediump float NoL, mediump float NoV, mediump vec3 specular, mediump float roughness)
{
    mediump float D = D_GGX(roughness, N, H);
    mediump float G = G_schlick(roughness, NoV, NoL);
    return specular * G * D;
}

mediump vec3 fresnel(mediump vec3 F0, mediump float HoV)
{
	return mix(F0, vec3(1.0), pow((1.0 - HoV), 5.0));
}

mediump vec3 fresnel_ibl(mediump vec3 F0, mediump float cos_theta, mediump float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cos_theta, 5.0);
}

mediump vec3 compute_F0(mediump vec3 base_color, mediump float metallic)
{
	return mix(vec3(0.04), base_color, metallic);
}

#endif
