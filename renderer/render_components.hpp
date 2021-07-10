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

#include "render_parameters.hpp"
#include "ecs.hpp"
#include "math.hpp"
#include "aabb.hpp"
#include "mesh.hpp"
#include "abstract_renderable.hpp"
#include "renderer_enums.hpp"
#include "camera.hpp"
#include "lights/lights.hpp"
#include "lights/volumetric_fog_region.hpp"

namespace Granite
{
class RenderGraph;
class RendererSuite;
class RenderQueue;
class RenderContext;
class RenderPass;
class Scene;
class Ground;
class PositionalLight;
class Skybox;
class TaskComposer;

struct Transform
{
	vec3 scale = vec3(1.0f);
	vec3 translation = vec3(0.0f);
	quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
};

struct CachedTransform
{
	mat4 world_transform;
	//mat4 normal_transform;
};

struct CachedSkinTransform
{
	std::vector<mat4> bone_world_transforms;
	//std::vector<mat4> bone_normal_transforms;
};

struct BoundedComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(BoundedComponent)
	const AABB *aabb;
};

struct UnboundedComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(UnboundedComponent)
};

struct BackgroundComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(BackgroundComponent)
};

struct EnvironmentComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(EnvironmentComponent)
	FogParameters fog;
};

struct RenderableComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(RenderableComponent)
	AbstractRenderableHandle renderable;
};

struct CameraComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(CameraComponent)
	Camera camera;
};

struct RenderPassCreator
{
	virtual ~RenderPassCreator() = default;
	virtual void add_render_passes(RenderGraph &graph) = 0;
	virtual void set_base_renderer(const RendererSuite *suite) = 0;
	virtual void set_base_render_context(const RenderContext *context) = 0;
	virtual void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target) = 0;
	virtual void setup_render_pass_resources(RenderGraph &graph) = 0;
	virtual void set_scene(Scene *scene) = 0;
};

struct RenderPassSinkComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(RenderPassSinkComponent)
};

struct CullPlaneComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(CullPlaneComponent)
	vec4 plane;
};

struct GroundComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(GroundComponent)
	Ground *ground = nullptr;
};

struct RenderPassComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(RenderPassComponent)
	RenderPassCreator *creator = nullptr;
};

struct PerFrameRefreshableTransform
{
	virtual ~PerFrameRefreshableTransform() = default;
	virtual void refresh(const RenderContext &context, const RenderInfoComponent *transform, TaskComposer &composer) = 0;
};

struct PerFrameRefreshable
{
	virtual ~PerFrameRefreshable() = default;
	virtual void refresh(const RenderContext &context, TaskComposer &composer) = 0;
};

struct PerFrameUpdateTransformComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(PerFrameUpdateTransformComponent)
	PerFrameRefreshableTransform *refresh = nullptr;
	int dependency_order = 0;
};

struct PerFrameUpdateComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(PerFrameUpdateComponent)
	PerFrameRefreshable *refresh = nullptr;
	int dependency_order = 0;
};

struct RenderInfoComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(RenderInfoComponent)
	AABB world_aabb;
	CachedTransform *transform = nullptr;
	CachedSkinTransform *skin_transform = nullptr;

	CachedTransform *prev_transform = nullptr;
	CachedSkinTransform *prev_skin_transform = nullptr;

	// If set, the transform changed last frame and motion vectors will need to be rendered explicitly.
	bool requires_motion_vectors = false;

	// Can be used to pass non-spatial transform related data to an AbstractRenderable,
	// e.g. per instance material information.
	const void *extra_data = nullptr;
};

struct CachedTransformComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(CachedTransformComponent)
	CachedTransform *transform = nullptr;
};

struct CachedSpatialTransformTimestampComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(CachedSpatialTransformTimestampComponent)
	uint64_t cookie = 0;
	Util::Hash timestamp_hash = 0;
	const uint32_t *current_timestamp = nullptr;
	uint32_t last_timestamp = ~0u;
};

struct OpaqueComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(OpaqueComponent)
};

struct TransparentComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(TransparentComponent)
};

struct PositionalLightComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(PositionalLightComponent)
	PositionalLight *light;
};

struct IrradianceAffectingComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(IrradianceAffectingComponent)
};

struct DirectionalLightComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(DirectionalLightComponent)
	vec3 color;
	vec3 direction;
};

struct HemisphereLightComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(HemisphereLightComponent)
	vec3 y_zero;
	vec3 y_up;
};

struct VolumetricDiffuseLightComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(VolumetricDiffuseLightComponent)
	VolumetricDiffuseLight light;
	vec4 world_to_texture[3];
	vec4 texture_to_world[3];
	vec4 world_lo;
	vec4 world_hi;
	uint32_t timestamp = 0;
	uint32_t update_iteration = 0;
};

struct VolumetricFogRegionComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(VolumetricFogRegionComponent)
	VolumetricFogRegion region;
	vec4 world_to_texture[3];
	vec4 world_lo;
	vec4 world_hi;
};

struct CastsStaticShadowComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(CastsStaticShadowComponent)
};

struct CastsDynamicShadowComponent : ComponentBase
{
	GRANITE_COMPONENT_TYPE_DECL(CastsDynamicShadowComponent)
};

}