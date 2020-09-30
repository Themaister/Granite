/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#pragma once

#include "abstract_renderable.hpp"
#include "scene.hpp"
#include "application_wsi_events.hpp"

namespace Granite
{
class Ground;
class GroundPatch : public AbstractRenderable, public PerFrameRefreshableTransform
{
public:
	friend class Ground;
	GroundPatch(Util::IntrusivePtr<Ground> ground);
	~GroundPatch();
	void set_bounds(vec3 offset, vec3 size);

	void set_lod_pointer(float *ptr)
	{
		lod = ptr;
	}

	void set_neighbors(const GroundPatch *nx_, const GroundPatch *px_, const GroundPatch *nz_, const GroundPatch *pz_)
	{
		nx = nx_;
		px = px_;
		nz = nz_;
		pz = pz_;
	}

private:
	Util::IntrusivePtr<Ground> ground;

	float *lod = nullptr;
	float lod_bias = 0.0f;

	// Neighbors
	const GroundPatch *nx = nullptr;
	const GroundPatch *px = nullptr;
	const GroundPatch *nz = nullptr;
	const GroundPatch *pz = nullptr;

	bool has_static_aabb() const override
	{
		return true;
	}

	const AABB *get_static_aabb() const override
	{
		return &aabb;
	}

	void get_render_info(const RenderContext &context, const RenderInfoComponent *transform, RenderQueue &queue) const override;
	vec2 offset = vec2(0.0f);
	vec2 size = vec2(1.0f);
	AABB aabb;

	void refresh(const RenderContext &context, const RenderInfoComponent *transform, TaskComposer &composer) override;
};

class Ground : public Util::IntrusivePtrEnabled<Ground>, public PerFrameRefreshable, public EventHandler
{
public:
	struct TerrainInfo
	{
		std::string heightmap;
		std::string normalmap;
		std::string occlusionmap;
		std::string base_color;
		std::string splatmap;
		std::string normalmap_fine;
		float lod_bias = 0.0f;
		unsigned base_patch_size = 64;
		unsigned normal_size = 1024;
		float max_lod = 5.0f;
		std::vector<float> patch_lod_bias;
		std::vector<vec2> patch_range;
		bool bandlimited_pixel = false;
	};
	Ground(unsigned size, const TerrainInfo &info);

	void set_tiling_factor(vec2 factor)
	{
		tiling_factor = factor;
	}

	struct Handles
	{
		Entity *entity;
		Scene::NodeHandle node;
		Ground *ground;
	};

	static Handles add_to_scene(Scene &scene, unsigned size, float tiling_factor, const TerrainInfo &info);

	void get_render_info(const RenderContext &context, const RenderInfoComponent *transform, RenderQueue &queue,
	                     const GroundPatch &patch) const;

	float *get_lod_pointer(unsigned x, unsigned z)
	{
		return &patch_lods[z * num_patches_x + x];
	}

	unsigned get_num_patches_x() const
	{
		return num_patches_x;
	}

	unsigned get_num_patches_z() const
	{
		return num_patches_z;
	}

	float get_base_lod_bias() const
	{
		return info.lod_bias;
	}

	const TerrainInfo &get_info() const
	{
		return info;
	}

private:
	unsigned size;
	TerrainInfo info;

	void refresh(const RenderContext &context, TaskComposer &composer) override;

	Vulkan::Texture *heights = nullptr;
	Vulkan::Texture *normals = nullptr;
	Vulkan::Texture *occlusion = nullptr;
	Vulkan::Texture *normals_fine = nullptr;
	Vulkan::Texture *base_color = nullptr;
	Vulkan::Texture *type_map = nullptr;
	Vulkan::ImageHandle lod_map;
	void on_device_created(const Vulkan::DeviceCreatedEvent &e);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &e);

	struct LOD
	{
		Vulkan::BufferHandle vbo;
		Vulkan::BufferHandle ibo;
		unsigned count;
	};
	std::vector<LOD> quad_lod;

	void build_buffers(Vulkan::Device &device);
	void build_lod(Vulkan::Device &device, unsigned size, unsigned stride);

	unsigned num_patches_x = 0;
	unsigned num_patches_z = 0;
	std::vector<float> patch_lods;

	vec2 tiling_factor = vec2(1.0f);
};
}
