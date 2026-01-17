/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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

#include "aabb.hpp"
#include "intrusive.hpp"
#include "material_manager.hpp"
#include "material_definitions.hpp"

namespace Granite
{
class RenderQueue;
class RenderContext;
class ShaderSuite;
struct RenderInfoComponent;
struct SpriteTransformInfo;

enum RenderableFlagBits
{
	RENDERABLE_FORCE_VISIBLE_BIT = 1 << 0,
	RENDERABLE_IMPLICIT_MOTION_BIT = 1 << 1,
	RENDERABLE_MESH_ASSET_BIT = 1 << 2,
	RENDERABLE_MESH_ASSET_SKINNED_BIT = 1 << 3,
};
using RenderableFlags = uint32_t;

class AbstractRenderable : public Util::IntrusivePtrEnabled<AbstractRenderable>
{
public:
	virtual ~AbstractRenderable() = default;
	virtual void get_render_info(const RenderContext &context, const RenderInfoComponent *transform,
	                             RenderQueue &queue) const = 0;

	virtual void get_depth_render_info(const RenderContext &context, const RenderInfoComponent *transform,
	                                   RenderQueue &queue) const
	{
		return get_render_info(context, transform, queue);
	}

	virtual void get_motion_vector_render_info(const RenderContext &context, const RenderInfoComponent *transform,
	                                           RenderQueue &queue) const
	{
		return get_render_info(context, transform, queue);
	}

	virtual void get_sprite_render_info(const SpriteTransformInfo &, RenderQueue &) const
	{
	}

	virtual bool has_static_aabb() const
	{
		return false;
	}

	virtual const AABB *get_static_aabb() const
	{
		static const AABB aabb(vec3(0.0f), vec3(0.0f));
		return &aabb;
	}

	virtual DrawPipeline get_mesh_draw_pipeline() const
	{
		return DrawPipeline::Opaque;
	}

	virtual size_t get_num_occluder_states() const
	{
		return 0;
	}

	RenderableFlags flags = 0;
};
using AbstractRenderableHandle = Util::IntrusivePtr<AbstractRenderable>;
using MeshAssetMaterialFlags = uint32_t;

// A specialized fixed function renderable that is intended to supplant StaticMesh and SkinnedMesh.
// Compatible with two-phase cull and optimized mesh/task rendering.
class MeshAssetRenderable final : public AbstractRenderable
{
public:
	MeshAssetRenderable(DrawPipeline pipeline, AssetID asset_id, const AABB &aabb_,
	                    size_t num_occluder_states_, MeshAssetMaterialFlags flags_)
	    : mesh_asset(asset_id)
	    , aabb(aabb_)
	    , draw_pipeline(pipeline)
	    , num_occluder_states(num_occluder_states_)
	    , material_flags(flags_)
	{
	}

	// This should not be used directly.
	void get_render_info(const RenderContext &, const RenderInfoComponent *, RenderQueue &) const override
	{
	}

	bool has_static_aabb() const override
	{
		return true;
	}

	const AABB *get_static_aabb() const override
	{
		return &aabb;
	}

	DrawPipeline get_mesh_draw_pipeline() const override
	{
		return draw_pipeline;
	}

	AssetID get_asset_id() const
	{
		return mesh_asset;
	}

	size_t get_num_occluder_states() const override
	{
		return num_occluder_states;
	}

	MeshAssetMaterialFlags get_material_flags() const
	{
		return material_flags;
	}

private:
	AssetID mesh_asset;
	AABB aabb;
	DrawPipeline draw_pipeline;
	size_t num_occluder_states;
	MeshAssetMaterialFlags material_flags;
};
} // namespace Granite
