#include "simd.hpp"
#include "muglm/muglm_impl.hpp"
#include <assert.h>
#include <string.h>

using namespace Granite;

int main()
{
	mat4 a(vec4(1, 2, 3, 4), vec4(5, 6, 7, 8), vec4(9, 10, 11, 12), vec4(13, 14, 15, 16));
	mat4 b(vec4(1, 2, 3, 4), vec4(-5, 6, 7, -8), vec4(9, 10, 11, 12), vec4(13, -14, 15, 16));
	mat4 ref = a * b;
	mat4 c;
	SIMD::mul(c, a, b);

	assert(memcmp(&ref, &c, sizeof(mat4)) == 0);
}