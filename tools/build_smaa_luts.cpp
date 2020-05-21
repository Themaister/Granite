#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"
#include "logging.hpp"
#include "memory_mapped_texture.hpp"
#include <string.h>

using namespace Granite;
using namespace Granite::SceneFormats;

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		LOGE("Usage: %s <AreaTex.gtx> <SearchTex.gtx>\n", argv[0]);
		return 1;
	}

	MemoryMappedTexture area;
	area.set_2d(VK_FORMAT_R8G8_UNORM, AREATEX_WIDTH, AREATEX_HEIGHT);
	if (!area.map_write(argv[1]))
	{
		LOGE("Failed to save area tex.\n");
		return 1;
	}

	MemoryMappedTexture search;
	search.set_2d(VK_FORMAT_R8_UNORM, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT);
	if (!search.map_write(argv[2]))
	{
		LOGE("Failed to save search tex.\n");
		return 1;
	}

	memcpy(area.get_layout().data(), areaTexBytes, AREATEX_SIZE);
	memcpy(search.get_layout().data(), searchTexBytes, SEARCHTEX_SIZE);
}
