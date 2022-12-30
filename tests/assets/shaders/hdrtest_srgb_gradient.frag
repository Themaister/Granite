#version 450

layout(location = 0) in vec3 vColor;
layout(location = 0) out vec3 FragColor;

layout(constant_id = 0) const bool hdr10 = false;

layout(set = 0, binding = 1) uniform Lum
{
	float lum;
};

vec3 srgb_to_linear(vec3 color)
{
    bvec3 isLo = lessThanEqual(color, vec3(0.04045f));

    vec3 loPart = color / 12.92f;
    vec3 hiPart = pow((color + 0.055f) / 1.055f, vec3(12.0f / 5.0f));
    return mix(hiPart, loPart, isLo);
}


vec3 linear_to_srgb(vec3 col)
{
	return mix(1.055 * pow(col, vec3(1.0 / 2.4)) - 0.055, col * 12.92, lessThanEqual(col, vec3(0.0031308)));
}

vec3 encode_transfer_function(vec3 nits)
{
	if (hdr10)
	{
		// PQ
		vec3 y = clamp(nits / 10000.0, vec3(0.0), vec3(1.0));
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
	else
	{
		vec3 n = clamp(nits / 100.0, vec3(0.0), vec3(1.0));
		return linear_to_srgb(n);
	}
}

void main()
{
	vec3 color = srgb_to_linear(vColor) * lum;
	FragColor = encode_transfer_function(color);
}
