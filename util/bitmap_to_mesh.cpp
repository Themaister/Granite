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
#include "meshoptimizer.h"
#include "hash.hpp"
#include <assert.h>
#include <list>
#include <algorithm>
#include <unordered_set>

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

	bool is_empty_clamped(int x, int y) const
	{
		if (x < 0 || y < 0)
			return true;
		if (unsigned(x) >= width || unsigned(y) >= height)
			return true;

		return at(unsigned(x), unsigned(y)) == PixelState::Empty;
	}

	bool rect_is_all_state(int x, int y, unsigned w, unsigned h, PixelState state) const
	{
		if (x < 0 || y < 0)
			return state == PixelState::Empty;
		if (unsigned(x) >= width || unsigned(y) >= height)
			return state == PixelState::Empty;

		for (unsigned j = y; j < y + h; j++)
			for (unsigned i = x; i < x + w; i++)
				if (at(i, j) != state)
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

struct ClaimedRect
{
	unsigned x = 0;
	unsigned y = 0;
	unsigned w = 0;
	unsigned h = 0;
	vector<unsigned> north_neighbors;
	vector<unsigned> east_neighbors;
	vector<unsigned> south_neighbors;
	vector<unsigned> west_neighbors;
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
		while (state.rect_is_all_state(xy_rect.x + xy_rect.w, xy_rect.y, 1, xy_rect.h, PixelState::Pending))
			xy_rect.w++;
		while (state.rect_is_all_state(xy_rect.x, xy_rect.y + xy_rect.h, xy_rect.w, 1, PixelState::Pending))
			xy_rect.h++;
	}

	ClaimedRect yx_rect = rect;
	{
		// Be greedy in Y, then in X.
		while (state.rect_is_all_state(yx_rect.x, yx_rect.y + yx_rect.h, yx_rect.w, 1, PixelState::Pending))
			yx_rect.h++;
		while (state.rect_is_all_state(yx_rect.x + yx_rect.w, yx_rect.y, 1, yx_rect.h, PixelState::Pending))
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

// Need to link up neighbors with "degenerate" triangles to get a 100% watertight mesh.
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

	// If the rects share a corner it is not necessary to emit degenerates.
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

	for (auto &n : rect.north_neighbors)
		emit_neighbor(position, rect, NeighborOrientation::North, all_rects[n]);
	for (auto &n : rect.east_neighbors)
		emit_neighbor(position, rect, NeighborOrientation::East, all_rects[n]);
	for (auto &n : rect.south_neighbors)
		emit_neighbor(position, rect, NeighborOrientation::South, all_rects[n]);
	for (auto &n : rect.west_neighbors)
		emit_neighbor(position, rect, NeighborOrientation::West, all_rects[n]);
}

static void emit_depth_links_north(const StateBitmap &state, vector<vec3> &depth_links,
                                   ClaimedRect &rect, vector<ClaimedRect> &rects)
{
	// North edge.
	if (state.rect_is_all_state(int(rect.x), int(rect.y) - 1, rect.w, 1, PixelState::Empty))
	{
		// Simple case, no degenerates needed.
		depth_links.emplace_back(float(rect.x + rect.w), 0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x + rect.w), -0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x), 0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x), -0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x), 0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x + rect.w), -0.5f, float(rect.y));
	}
	else
	{
		// Partial case. Need to create degenerates to link up.
		assert(rect.y > 0);

		unsigned start_empty_x = rect.x;
		while (start_empty_x < rect.x + rect.w)
		{
			while (start_empty_x < rect.x + rect.w)
			{
				if (state.at(start_empty_x, rect.y - 1) == PixelState::Empty)
					break;
				start_empty_x++;
			}

			if (start_empty_x < rect.x + rect.w)
			{
				unsigned end_empty_x = start_empty_x + 1;
				while (end_empty_x < rect.x + rect.w)
				{
					if (state.at(end_empty_x, rect.y - 1) == PixelState::Empty)
						end_empty_x++;
					else
						break;
				}

				ClaimedRect neighbor;
				neighbor.x = start_empty_x;
				neighbor.w = end_empty_x - start_empty_x;
				neighbor.y = rect.y - 1;
				neighbor.h = 1;
				rect.north_neighbors.push_back(rects.size());
				rects.push_back(neighbor);

				depth_links.emplace_back(float(end_empty_x), 0.5f, float(rect.y));
				depth_links.emplace_back(float(end_empty_x), -0.5f, float(rect.y));
				depth_links.emplace_back(float(start_empty_x), 0.5f, float(rect.y));
				depth_links.emplace_back(float(start_empty_x), -0.5f, float(rect.y));
				depth_links.emplace_back(float(start_empty_x), 0.5f, float(rect.y));
				depth_links.emplace_back(float(end_empty_x), -0.5f, float(rect.y));

				start_empty_x = end_empty_x + 1;
			}
		}
	}
}

static void emit_depth_links_south(const StateBitmap &state, vector<vec3> &depth_links,
                                   ClaimedRect &rect, vector<ClaimedRect> &rects)
{
	// South edge.
	if (state.rect_is_all_state(int(rect.x), int(rect.y + rect.h), rect.w, 1, PixelState::Empty))
	{
		// Simple case, no degenerates needed.
		depth_links.emplace_back(float(rect.x), 0.5f, float(rect.y + rect.h));
		depth_links.emplace_back(float(rect.x), -0.5f, float(rect.y + rect.h));
		depth_links.emplace_back(float(rect.x + rect.w), 0.5f, float(rect.y + rect.h));
		depth_links.emplace_back(float(rect.x + rect.w), -0.5f, float(rect.y + rect.h));
		depth_links.emplace_back(float(rect.x + rect.w), 0.5f, float(rect.y + rect.h));
		depth_links.emplace_back(float(rect.x), -0.5f, float(rect.y + rect.h));
	}
	else
	{
		// Partial case. Need to create degenerates to link up.
		unsigned start_empty_x = rect.x;
		while (start_empty_x < rect.x + rect.w)
		{
			while (start_empty_x < rect.x + rect.w)
			{
				if (state.at(start_empty_x, rect.y + rect.h) == PixelState::Empty)
					break;
				start_empty_x++;
			}

			if (start_empty_x < rect.x + rect.w)
			{
				unsigned end_empty_x = start_empty_x + 1;
				while (end_empty_x < rect.x + rect.w)
				{
					if (state.at(end_empty_x, rect.y + rect.h) == PixelState::Empty)
						end_empty_x++;
					else
						break;
				}

				ClaimedRect neighbor;
				neighbor.x = start_empty_x;
				neighbor.w = end_empty_x - start_empty_x;
				neighbor.y = rect.y + rect.h;
				neighbor.h = 1;
				rect.south_neighbors.push_back(rects.size());
				rects.push_back(neighbor);

				depth_links.emplace_back(float(start_empty_x), 0.5f, float(rect.y + rect.h));
				depth_links.emplace_back(float(start_empty_x), -0.5f, float(rect.y + rect.h));
				depth_links.emplace_back(float(end_empty_x), 0.5f, float(rect.y + rect.h));
				depth_links.emplace_back(float(end_empty_x), -0.5f, float(rect.y + rect.h));
				depth_links.emplace_back(float(end_empty_x), 0.5f, float(rect.y + rect.h));
				depth_links.emplace_back(float(start_empty_x), -0.5f, float(rect.y + rect.h));

				start_empty_x = end_empty_x + 1;
			}
		}
	}
}

static void emit_depth_links_east(const StateBitmap &state, vector<vec3> &depth_links,
                                  ClaimedRect &rect, vector<ClaimedRect> &rects)
{
	// South edge.
	if (state.rect_is_all_state(int(rect.x + rect.w), int(rect.y), 1, rect.h, PixelState::Empty))
	{
		// Simple case, no degenerates needed.
		depth_links.emplace_back(float(rect.x + rect.w), 0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x + rect.w), 0.5f, float(rect.y + rect.h));
		depth_links.emplace_back(float(rect.x + rect.w), -0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x + rect.w), -0.5f, float(rect.y + rect.h));
		depth_links.emplace_back(float(rect.x + rect.w), -0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x + rect.w), 0.5f, float(rect.y + rect.h));
	}
	else
	{
		// Partial case. Need to create degenerates to link up.
		unsigned start_empty_y = rect.y;
		while (start_empty_y < rect.y + rect.h)
		{
			while (start_empty_y < rect.y + rect.h)
			{
				if (state.at(rect.x + rect.w, start_empty_y) == PixelState::Empty)
					break;
				start_empty_y++;
			}

			if (start_empty_y < rect.y + rect.h)
			{
				unsigned end_empty_y = start_empty_y + 1;
				while (end_empty_y < rect.y + rect.h)
				{
					if (state.at(rect.x + rect.w, end_empty_y) == PixelState::Empty)
						end_empty_y++;
					else
						break;
				}

				ClaimedRect neighbor;
				neighbor.x = rect.x + rect.w;
				neighbor.w = 1;
				neighbor.y = start_empty_y;
				neighbor.h = end_empty_y - start_empty_y;
				rect.east_neighbors.push_back(rects.size());
				rects.push_back(neighbor);

				depth_links.emplace_back(float(rect.x + rect.w), 0.5f, float(start_empty_y));
				depth_links.emplace_back(float(rect.x + rect.w), 0.5f, float(end_empty_y));
				depth_links.emplace_back(float(rect.x + rect.w), -0.5f, float(start_empty_y));
				depth_links.emplace_back(float(rect.x + rect.w), -0.5f, float(end_empty_y));
				depth_links.emplace_back(float(rect.x + rect.w), -0.5f, float(start_empty_y));
				depth_links.emplace_back(float(rect.x + rect.w), 0.5f, float(end_empty_y));

				start_empty_y = end_empty_y + 1;
			}
		}
	}
}

static void emit_depth_links_west(const StateBitmap &state, vector<vec3> &depth_links,
                                  ClaimedRect &rect, vector<ClaimedRect> &rects)
{
	// South edge.
	if (state.rect_is_all_state(int(rect.x) - 1, int(rect.y), 1, rect.h, PixelState::Empty))
	{
		// Simple case, no degenerates needed.
		depth_links.emplace_back(float(rect.x), -0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x), -0.5f, float(rect.y + rect.h));
		depth_links.emplace_back(float(rect.x), 0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x), 0.5f, float(rect.y + rect.h));
		depth_links.emplace_back(float(rect.x), 0.5f, float(rect.y));
		depth_links.emplace_back(float(rect.x), -0.5f, float(rect.y + rect.h));
	}
	else
	{
		assert(rect.x > 0);
		// Partial case. Need to create degenerates to link up.
		unsigned start_empty_y = rect.y;
		while (start_empty_y < rect.y + rect.h)
		{
			while (start_empty_y < rect.y + rect.h)
			{
				if (state.at(rect.x - 1, start_empty_y) == PixelState::Empty)
					break;
				start_empty_y++;
			}

			if (start_empty_y < rect.y + rect.h)
			{
				unsigned end_empty_y = start_empty_y + 1;
				while (end_empty_y < rect.y + rect.h)
				{
					if (state.at(rect.x - 1, end_empty_y) == PixelState::Empty)
						end_empty_y++;
					else
						break;
				}

				ClaimedRect neighbor;
				neighbor.x = rect.x - 1;
				neighbor.w = 1;
				neighbor.y = start_empty_y;
				neighbor.h = end_empty_y - start_empty_y;
				rect.west_neighbors.push_back(rects.size());
				rects.push_back(neighbor);

				depth_links.emplace_back(float(rect.x), -0.5f, float(start_empty_y));
				depth_links.emplace_back(float(rect.x), -0.5f, float(end_empty_y));
				depth_links.emplace_back(float(rect.x), 0.5f, float(start_empty_y));
				depth_links.emplace_back(float(rect.x), 0.5f, float(end_empty_y));
				depth_links.emplace_back(float(rect.x), 0.5f, float(start_empty_y));
				depth_links.emplace_back(float(rect.x), -0.5f, float(end_empty_y));

				start_empty_y = end_empty_y + 1;
			}
		}
	}
}

static void emit_depth_links(const StateBitmap &state, vector<vec3> &depth_links,
                             ClaimedRect &rect, vector<ClaimedRect> &rects)
{
	emit_depth_links_north(state, depth_links, rect, rects);
	emit_depth_links_south(state, depth_links, rect, rects);
	emit_depth_links_east(state, depth_links, rect, rects);
	emit_depth_links_west(state, depth_links, rect, rects);
}

static void compute_normals(vector<vec3> &normals, const vector<vec3> &positions)
{
	for (size_t i = 0; i < positions.size(); i += 3)
	{
		vec3 normal = sign(cross(positions[i + 1] - positions[i + 0], positions[i + 2] - positions[i + 0]));
		if (all(equal(normal, vec3(0.0f))))
			normal.y = sign(positions[i].y);

		for (size_t j = 0; j < 3; j++)
			normals[i + j] = normal;
	}
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
				rects[i].north_neighbors.push_back(j);
			else if (is_east_neighbor(rects[i], rects[j]))
				rects[i].east_neighbors.push_back(j);
			else if (is_south_neighbor(rects[i], rects[j]))
				rects[i].south_neighbors.push_back(j);
			else if (is_west_neighbor(rects[i], rects[j]))
				rects[i].west_neighbors.push_back(j);
		}
	}

	vector<vec3> depth_link_position;
	unsigned primary_rects = rects.size();
	for (unsigned i = 0; i < primary_rects; i++)
	{
		// rects[i] might be invalidated if rects changes, so move into a temporary.
		auto r = move(rects[i]);
		emit_depth_links(state, depth_link_position, r, rects);
		rects[i] = move(r);
	}

	vector<vec3> positions;
	for (unsigned i = 0; i < primary_rects; i++)
		emit_rect(positions, rects[i], rects);

	vector<vec3> back_positions;
	back_positions.reserve(positions.size());
	for (auto itr = begin(positions); itr != end(positions); )
	{
		itr->y = 0.5f;
		auto v0 = *itr++;
		itr->y = 0.5f;
		auto v1 = *itr++;
		itr->y = 0.5f;
		auto v2 = *itr++;
		back_positions.emplace_back(v0.x, -v0.y, v0.z);
		back_positions.emplace_back(v2.x, -v2.y, v2.z);
		back_positions.emplace_back(v1.x, -v1.y, v1.z);
	}

	positions.insert(end(positions), begin(back_positions), end(back_positions));
	positions.insert(end(positions), begin(depth_link_position), end(depth_link_position));

	vector<vec3> normals(positions.size());
	compute_normals(normals, positions);

	vector<uint32_t> output_indices(positions.size());
	meshopt_Stream streams[2] = {};
	streams[0].data = positions.data();
	streams[0].size = sizeof(vec3);
	streams[0].stride = sizeof(vec3);
	streams[1].data = normals.data();
	streams[1].size = sizeof(vec3);
	streams[1].stride = sizeof(vec3);
	size_t unique_vertices = meshopt_generateVertexRemapMulti(output_indices.data(), nullptr, positions.size(), positions.size(), streams, 2);

	meshopt_remapVertexBuffer(positions.data(), positions.data(), positions.size(), sizeof(vec3), output_indices.data());
	meshopt_remapVertexBuffer(normals.data(), normals.data(), normals.size(), sizeof(vec3), output_indices.data());
	positions.resize(unique_vertices);
	normals.resize(unique_vertices);

	bitmap.positions = move(positions);
	bitmap.normals = move(normals);
	bitmap.indices.reserve(output_indices.size());

	// We might emit duplicate primitives, remove them.
	unordered_set<Util::Hash> seen_primitives;
	seen_primitives.reserve(output_indices.size() / 3);
	for (size_t i = 0; i < output_indices.size(); i += 3)
	{
		for (unsigned j = 0; j < 3; j++)
		{
			Util::Hasher h;
			h.u32(output_indices[i + (j + 0) % 3]);
			h.u32(output_indices[i + (j + 1) % 3]);
			h.u32(output_indices[i + (j + 2) % 3]);
			if (seen_primitives.count(h.get()))
				goto out;
			else
				seen_primitives.insert(h.get());
		}

		for (unsigned j = 0; j < 3; j++)
			bitmap.indices.push_back(output_indices[i + j]);
out:;
	}
	return true;
}
}