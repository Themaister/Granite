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
	void set_scale(vec2 base, vec2 offset);

private:
	Util::IntrusivePtr<Ground> ground;

	float lod = 0.0f;
	float lod_bias = 0.0f;

	// Neighbors
	GroundPatch *nx = nullptr;
	GroundPatch *px = nullptr;
	GroundPatch *nz = nullptr;
	GroundPatch *pz = nullptr;

	bool has_static_aabb() const override
	{
		return true;
	}

	const AABB &get_static_aabb() const
	{
		return aabb;
	}

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform, RenderQueue &queue) const override;
	vec2 base = vec2(0.0f);
	vec2 offset = vec2(1.0f);
	AABB aabb;

	void refresh(RenderContext &context, const CachedSpatialTransformComponent *transform) override;
};

class Ground : public Util::IntrusivePtrEnabled<Ground>, public PerFrameRefreshable, public EventHandler
{
public:
	struct Handles
	{
		EntityHandle entity;
		Scene::NodeHandle node;
	};
	static Handles add_to_scene(Scene &scene, const std::string &heightmap, const std::string &normalmap);

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform, RenderQueue &queue,
                         const vec2 &base, const vec2 &offset) const;

private:
	Ground(const std::string &heightmap, const std::string &normalmap);
	std::string heightmap_path;
	std::string normalmap_path;

	void refresh(RenderContext &context) override;
	Vulkan::Texture *heights = nullptr;
	Vulkan::Texture *normals = nullptr;
	void on_device_created(const Event &e);
	void on_device_destroyed(const Event &e);
};
}