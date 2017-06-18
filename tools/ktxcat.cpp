#include "gli/load.hpp"
#include "gli/save.hpp"
#include "util.hpp"
#include <vector>
#include <gli/texture2d_array.hpp>

using namespace std;

int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		LOGE("Usage: %s <output> <inputs>...\n", argv[0]);
		return 1;
	}

	vector<gli::texture> inputs;
	gli::format fmt = gli::FORMAT_UNDEFINED;
	int width = 0;
	int height = 0;
	unsigned levels = 0;

	for (int i = 2; i < argc; i++)
	{
		gli::texture tex = gli::load(argv[i]);
		if (tex.empty())
		{
			LOGE("Failed to load texture: %s\n", argv[i]);
			return 1;
		}

		if (fmt != gli::FORMAT_UNDEFINED)
		{
			if (tex.format() != fmt)
			{
				LOGE("Format mismatch!\n");
				return 1;
			}

			if (tex.extent().x != width)
			{
				LOGE("Mismatch width\n");
				return 1;
			}

			if (tex.extent().y != height)
			{
				LOGE("Mismatch height\n");
				return 1;
			}

			if (tex.levels() != levels)
			{
				LOGE("Mismatch levels\n");
				return 1;
			}
		}

		if (tex.target() != gli::TARGET_2D)
		{
			LOGE("Input can only be 2D textures\n");
			return 1;
		}

		fmt = tex.format();
		width = tex.extent().x;
		height = tex.extent().y;
		levels = tex.levels();
		inputs.push_back(move(tex));
	}

	gli::texture2d_array array(fmt, gli::extent2d(width, height), inputs.size(), levels);
	for (unsigned layer = 0; layer < inputs.size(); layer++)
	{
		for (unsigned level = 0; level < levels; level++)
		{
			auto *dst = array.data(layer, 0, level);
			auto *src = inputs[layer].data(0, 0, level);

			size_t dst_size = array.size(level);
			size_t src_size = array.size(level);
			if (dst_size != src_size)
			{
				LOGE("Size mismatch!\n");
				return 1;
			}

			memcpy(dst, src, dst_size);
		}
	}

	if (!gli::save(array, argv[1]))
	{
		LOGE("Failed to save file: %s\n", argv[1]);
		return 1;
	}

	return 0;
}