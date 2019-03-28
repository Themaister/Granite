/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "bitmap_to_mesh.hpp"
#include <assert.h>
#include <list>

using namespace std;

namespace Granite
{
enum class PixelState : uint8_t
{
	Empty,
	Pending,
	Claimed
};

enum class NeighborOrientation : uint8_t
{
	North,
	East,
	South,
	West,
};

class StateBitmap
{
public:
	StateBitmap(unsigned width_, unsigned height_)
		: width(width_), height(height_)
	{
		state_bitmap.resize(width * height);
		state_nodes.resize(width * height);
	}

	PixelState &at(unsigned x, unsigned y)
	{
		return state_bitmap[y * width + x];
	}

	const PixelState &at(unsigned x, unsigned y) const
	{
		return state_bitmap[y * width + x];
	}

	bool rect_is_all_pending(unsigned x, unsigned y, unsigned w, unsigned h) const
	{
		if (x + w > width)
			return false;
		if (y + h > height)
			return false;

		for (unsigned j = y; j < y + h; j++)
			for (unsigned i = x; i < x + w; i++)
				if (at(i, j) != PixelState::Pending)
					return false;

		return true;
	}

	void claim_rect(unsigned x, unsigned y, unsigned w, unsigned h)
	{
		for (unsigned j = y; j < y + h; j++)
		{
			for (unsigned i = x; i < x + w; i++)
			{
				assert(at(i, j) == PixelState::Pending);
				at(i, j) = PixelState::Claimed;
				pending_pixels.erase(state_nodes[j * width + i]);
			}
		}
	}

	bool pop_next_pending(uvec2 &coord)
	{
		if (pending_pixels.empty())
			return false;

		coord = pending_pixels.front();
		pending_pixels.erase(pending_pixels.begin());
		return true;
	}

	void add_pending(unsigned x, unsigned y)
	{
		auto itr = pending_pixels.insert(pending_pixels.end(), uvec2(x, y));
		state_nodes[y * width + x] = itr;
	}

private:
	unsigned width, height;
	vector<PixelState> state_bitmap;
	vector<list<uvec2>::iterator> state_nodes;
	list<uvec2> pending_pixels;
};

struct Neighbor
{
	unsigned index;
	NeighborOrientation orientation;
};

struct ClaimedRect
{
	unsigned x = 0;
	unsigned y = 0;
	unsigned w = 0;
	unsigned h = 0;
	vector<Neighbor> neighbors;
};

static ClaimedRect find_largest_pending_rect(StateBitmap &state, unsigned x, unsigned y)
{
	ClaimedRect rect;
	rect.x = x;
	rect.y = y;
	rect.w = 1;
	rect.h = 1;

	ClaimedRect xy_rect = rect;
	{
		// Be greedy in X, then in Y.
		while (state.rect_is_all_pending(xy_rect.x + xy_rect.w, xy_rect.y, 1, xy_rect.h))
			xy_rect.w++;
		while (state.rect_is_all_pending(xy_rect.x, xy_rect.y + xy_rect.h, xy_rect.w, 1))
			xy_rect.h++;
	}

	ClaimedRect yx_rect = rect;
	{
		// Be greedy in Y, then in X.
		while (state.rect_is_all_pending(yx_rect.x, yx_rect.y + yx_rect.h, yx_rect.w, 1))
			yx_rect.h++;
		while (state.rect_is_all_pending(yx_rect.x + yx_rect.w, yx_rect.y, 1, yx_rect.h))
			yx_rect.w++;
	}

	// Accept the one with largest area (greedy).
	if (xy_rect.w * xy_rect.h < yx_rect.w * yx_rect.h)
		return yx_rect;
	else
		return xy_rect;
}

bool voxelize_bitmap(VoxelizedBitmap &bitmap, const uint8_t *components, unsigned component, unsigned pixel_stride,
                     unsigned width, unsigned height, unsigned row_stride)
{
	bitmap = {};

	StateBitmap state(width, height);
	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			bool active = components[component + pixel_stride * x + y * row_stride] >= 128;
			state.at(x, y) = active ? PixelState::Pending : PixelState::Empty;
			if (active)
				state.add_pending(x, y);
		}
	}

	// Create all rects which the bitmap is made of.
	vector<ClaimedRect> rects;

	uvec2 coord;
	while (state.pop_next_pending(coord))
	{
		ClaimedRect rect = find_largest_pending_rect(state, coord.x, coord.y);
		rects.push_back(rect);
	}

	// Find all adjacent neighbors. We will need to emit degenerate triangles to get water-tight meshes.

	return true;
}
}