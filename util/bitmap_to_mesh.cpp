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
#include "muglm/muglm_impl.hpp"
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

	bool get_next_pending(uvec2 &coord)
	{
		if (pending_pixels.empty())
			return false;

		coord = pending_pixels.front();
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

static bool horizontal_overlap(const ClaimedRect &a, const ClaimedRect &b)
{
	if (a.x + a.w <= b.x)
		return false;
	if (b.x + b.w <= a.x)
		return false;

	return true;
}

static bool vertical_overlap(const ClaimedRect &a, const ClaimedRect &b)
{
	if (a.y + a.h <= b.y)
		return false;
	if (b.y + b.h <= a.y)
		return false;

	return true;
}

static bool is_north_neighbor(const ClaimedRect &a, const ClaimedRect &b)
{
	// Check for overlap on north edge.
	if (b.y + b.h != a.y)
		return false;
	return horizontal_overlap(a, b);
}

static bool is_east_neighbor(const ClaimedRect &a, const ClaimedRect &b)
{
	// Check for overlap on east edge.
	if (a.x + a.w != b.x)
		return false;
	return vertical_overlap(a, b);
}

static bool is_south_neighbor(const ClaimedRect &a, const ClaimedRect &b)
{
	// Check for overlap on south edge.
	if (a.y + a.h != b.y)
		return false;
	return horizontal_overlap(a, b);
}

static bool is_west_neighbor(const ClaimedRect &a, const ClaimedRect &b)
{
	// Check for overlap on west edge.
	if (b.x + b.w != a.x)
		return false;
	return vertical_overlap(a, b);
}

static bool is_degenerate(const vec3 &a, const vec3 &b, const vec3 &c)
{
	return all(equal(a, b)) || all(equal(a, c)) || all(equal(b, c));
}

static void emit_neighbor(vector<vec3> &position,
                          const ClaimedRect &rect,
                          NeighborOrientation orientation,
                          const ClaimedRect &neighbor)
{
	const float e = 0.0f;
	vec3 coords[4];

	switch (orientation)
	{
	case NeighborOrientation::North:
		coords[0] = vec3(float(rect.x) + e, 0.0f, float(rect.y) + e);
		coords[1] = vec3(float(rect.x + rect.w) - e, 0.0f, float(rect.y) + e);
		coords[2] = vec3(float(neighbor.x + neighbor.w) - e, 0.0f, float(neighbor.y + neighbor.h) - e);
		coords[3] = vec3(float(neighbor.x) + e, 0.0f, float(neighbor.y + neighbor.h) - e);
		break;

	case NeighborOrientation::South:
		coords[0] = vec3(float(neighbor.x) + e, 0.0f, float(neighbor.y) + e);
		coords[1] = vec3(float(neighbor.x + neighbor.w) - e, 0.0f, float(neighbor.y) + e);
		coords[2] = vec3(float(rect.x + rect.w) - e, 0.0f, float(rect.y + rect.h) - e);
		coords[3] = vec3(float(rect.x) + e, 0.0f, float(rect.y + rect.h) - e);
		break;

	case NeighborOrientation::East:
		coords[0] = vec3(float(rect.x + rect.w) - e, 0.0f, float(rect.y) + e);
		coords[1] = vec3(float(rect.x + rect.w) - e, 0.0f, float(rect.y + rect.h) - e);
		coords[2] = vec3(float(neighbor.x) + e, 0.0f, float(neighbor.y + neighbor.h) - e);
		coords[3] = vec3(float(neighbor.x) + e, 0.0f, float(neighbor.y) + e);
		break;

	case NeighborOrientation::West:
		coords[0] = vec3(float(neighbor.x + neighbor.w) - e, 0.0f, float(neighbor.y) + e);
		coords[1] = vec3(float(neighbor.x + neighbor.w) - e, 0.0f, float(neighbor.y + neighbor.h) - e);
		coords[2] = vec3(float(rect.x) + e, 0.0f, float(rect.y + rect.h) - e);
		coords[3] = vec3(float(rect.x) + e, 0.0f, float(rect.y) + e);
		break;
	}

	if (!is_degenerate(coords[0], coords[1], coords[2]))
	{
		position.push_back(coords[0]);
		position.push_back(coords[1]);
		position.push_back(coords[2]);
	}

	if (!is_degenerate(coords[3], coords[0], coords[2]))
	{
		position.push_back(coords[3]);
		position.push_back(coords[0]);
		position.push_back(coords[2]);
	}
}

static void emit_rect(vector<vec3> &position, const ClaimedRect &rect, const vector<ClaimedRect> &all_rects)
{
	const float e = 0.0f;
	position.emplace_back(float(rect.x) + e, 0.0f, float(rect.y) + e);
	position.emplace_back(float(rect.x) + e, 0.0f, float(rect.y + rect.h) - e);
	position.emplace_back(float(rect.x + rect.w) - e, 0.0f, float(rect.y) + e);
	position.emplace_back(float(rect.x + rect.w) - e, 0.0f, float(rect.y + rect.h) - e);
	position.emplace_back(float(rect.x + rect.w) - e, 0.0f, float(rect.y) + e);
	position.emplace_back(float(rect.x) + e, 0.0f, float(rect.y + rect.h) - e);

	for (auto &n : rect.neighbors)
		emit_neighbor(position, rect, n.orientation, all_rects[n.index]);
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
	while (state.get_next_pending(coord))
	{
		ClaimedRect rect = find_largest_pending_rect(state, coord.x, coord.y);
		state.claim_rect(rect.x, rect.y, rect.w, rect.h);
		rects.push_back(rect);
	}

	// Find all adjacent neighbors. We will need to emit degenerate triangles to get water-tight meshes.
	// FIXME: Slow, O(n^2).
	unsigned num_rects = rects.size();
	for (unsigned i = 0; i < num_rects; i++)
	{
		for (unsigned j = i + 1; j < num_rects; j++)
		{
			if (is_north_neighbor(rects[i], rects[j]))
				rects[i].neighbors.push_back({ j, NeighborOrientation::North });
			else if (is_east_neighbor(rects[i], rects[j]))
				rects[i].neighbors.push_back({ j, NeighborOrientation::East });
			else if (is_south_neighbor(rects[i], rects[j]))
				rects[i].neighbors.push_back({ j, NeighborOrientation::South });
			else if (is_west_neighbor(rects[i], rects[j]))
				rects[i].neighbors.push_back({ j, NeighborOrientation::West });
		}
	}

	vector<vec3> positions;
	for (auto &rect : rects)
		emit_rect(positions, rect, rects);

	bitmap.positions = move(positions);
	return true;
}
}