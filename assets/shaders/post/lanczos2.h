#ifndef LANCZOS2_H_
#define LANCZOS2_H_

#ifndef PI
#define PI 3.1415628
#endif

// Simple and naive sinc scaler, slow impl.
// Placeholder implementation.

mediump float sinc(mediump float v)
{
	if (abs(v) < 0.0001)
	{
		return 1.0;
	}
	else
	{
		v *= PI;
		return sin(v) / v;
	}
}

mediump float kernel(mediump float v)
{
	return sinc(v) * sinc(v * 0.5);
}

mediump float weight(mediump float x, mediump float y)
{
	return kernel(x) * kernel(y);
}

mediump vec3 lanczos2(mediump sampler2D tex, vec2 unnormalized_coord, vec2 inv_resolution)
{
	unnormalized_coord -= 0.5;
	vec2 i_coord = floor(unnormalized_coord);
	vec2 f_coord = unnormalized_coord - i_coord;
	vec2 uv = (i_coord + 0.5) * inv_resolution;

mediump vec3 color = vec3(0.0);

	float total_w = 0.0;

#define TAP(X, Y) { \
    mediump float w = weight(f_coord.x - float(X), f_coord.y - float(Y)); \
    mediump vec3 col = textureLodOffset(tex, uv, 0.0, ivec2(X, Y)).rgb; \
    color += col * w; \
    total_w += w; \
}

#define TAPS(l) TAP(-1, l); TAP(+0, l); TAP(+1, l); TAP(+2, l)
	TAPS(-1);
	TAPS(+0);
	TAPS(+1);
	TAPS(+2);

	color /= total_w;
	return color;
}

#endif