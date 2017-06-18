#pragma once

#include "abstract_renderable.hpp"
#include "scene.hpp"

namespace Granite
{
class Ground;
class GroundPatch : public AbstractRenderable, public PerFrameRefreshableTransform
{
public:
	friend class Ground;
	GroundPatch(Util::IntrusivePtr<Ground> ground);
	void set_scale(vec2 offset, vec2 size);

	void set_lod_pointer(float *ptr)
	{
		lod = ptr;
	}

	void set_neighbors(const GroundPatch *nx, const GroundPatch *px, const GroundPatch *nz, const GroundPatch *pz)
	{
		this->nx = nx;
		this->px = px;
		this->nz = nz;
		this->pz = pz;
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

	const AABB &get_static_aabb() const override
	{
		return aabb;
	}

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform, RenderQueue &queue) const override;
	vec2 offset = vec2(0.0f);
	vec2 size = vec2(1.0f);
	AABB aabb;

	void refresh(RenderContext &context, const CachedSpatialTransformComponent *transform) override;
};

class Ground : public Util::IntrusivePtrEnabled<Ground>, public PerFrameRefreshable, public EventHandler
{
public:
	Ground(unsigned size, const std::string &heightmap, const std::string &normalmap, const std::string &base_color);

	void set_tiling_factor(vec2 factor)
	{
		tiling_factor = factor;
	}

	struct Handles
	{
		EntityHandle entity;
		Scene::NodeHandle node;
		Ground *ground;
	};

	static Handles add_to_scene(Scene &scene, unsigned size, float tiling_factor,
	                            const std::string &heightmap, const std::string &normalmap,
	                            const std::string &base_color);

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform, RenderQueue &queue,
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

	static const unsigned base_patch_size;
	static const float max_lod;

private:
	unsigned size;
	std::string heightmap_path;
	std::string normalmap_path;
	std::string base_color_path;

	void refresh(RenderContext &context) override;
	Vulkan::Texture *heights = nullptr;
	Vulkan::Texture *normals = nullptr;
	Vulkan::Texture *base_color = nullptr;
	Vulkan::ImageHandle lod_map;
	Vulkan::ImageHandle type_map;
	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);

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
