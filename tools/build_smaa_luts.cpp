#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"
#include "util.hpp"
#include "gli/texture.hpp"
#include "gli/save.hpp"

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		LOGE("Usage: %s <AreaTex.ktx> <SearchTex.ktx>\n", argv[0]);
		return 1;
	}

	gli::texture area_tex(gli::TARGET_2D, gli::FORMAT_RG8_UNORM_PACK8,
	                      gli::extent3d(AREATEX_WIDTH, AREATEX_HEIGHT, 1),
	                      1, 1, 1);
	gli::texture search_tex(gli::TARGET_2D, gli::FORMAT_R8_UNORM_PACK8,
	                        gli::extent3d(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1),
	                        1, 1, 1);

	memcpy(area_tex.data(), areaTexBytes, AREATEX_SIZE);
	memcpy(search_tex.data(), searchTexBytes, SEARCHTEX_SIZE);

	if (!gli::save(area_tex, argv[1]))
	{
		LOGE("Failed to save area tex.\n");
		return 1;
	}

	if (!gli::save(search_tex, argv[2]))
	{
		LOGE("Failed to save search tex.\n");
		return 1;
	}
}