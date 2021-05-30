#ifndef REPROJECTION_COLOR_SPACE_H_
#define REPROJECTION_COLOR_SPACE_H_

// max3 based tonemapper.
mediump float RCP(mediump float v)
{
	return 1.0 / v;
}

mediump float Max3(mediump float x, mediump float y, mediump float z)
{
	return max(x, max(y, z));
}

mediump vec3 Tonemap(mediump vec3 c)
{
	// Saturate the tonemapper a bit so we get non-linear behavior earlier.
	c *= 8.0;
	return c * RCP(Max3(c.r, c.g, c.b) + 1.0);
}

mediump vec3 TonemapInvert(mediump vec3 c)
{
	// Saturate the tonemapper a bit so we get non-linear behavior earlier.
	return (1.0 / 8.0) * c * RCP(1.0 - Max3(c.r, c.g, c.b));
}

mediump vec3 RGB_to_YCgCo(mediump vec3 c)
{
	return vec3(
		0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
		0.5 * c.g - 0.25 * c.r - 0.25 * c.b,
		0.5 * c.r - 0.5 * c.b);
}

mediump vec3 YCgCo_to_RGB(mediump vec3 c)
{
	mediump float tmp = c.x - c.y;
	return vec3(tmp + c.z, c.x + c.y, tmp - c.z);
	// c.x - c.y + c.z = [0.25, 0.5, 0.25] - [-0.25, 0.5, -0.25] + [0.5, 0.0, -0.5] = [1.0, 0.0, 0.0]
	// c.x + c.y       = [0.25, 0.5, 0.25] + [-0.25, 0.5, -0.25]                    = [0.0, 1.0, 0.0]
	// c.x - c.y - c.z = [0.25, 0.5, 0.25] - [-0.25, 0.5, -0.25] - [0.5, 0.0, -0.5] = [0.0, 0.0, 1.0]
}

mediump vec3 HDRColorSpaceToTAA(mediump vec3 color)
{
	return RGB_to_YCgCo(Tonemap(color));
}

mediump vec3 TAAToHDRColorSpace(mediump vec3 color)
{
	return TonemapInvert(clamp(YCgCo_to_RGB(color), 0.0, 0.999));
}

#endif