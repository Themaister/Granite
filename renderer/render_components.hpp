/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

#include <render_parameters.hpp>
#include "ecs.hpp"
#include "math.hpp"
#include "aabb.hpp"
#include "mesh.hpp"
#include "abstract_renderable.hpp"
#include "renderer_enums.hpp"
#include "camera.hpp"

namespace Granite
{
class RenderGraph;
class Renderer;
class RenderQueue;
class RenderContext;
class RenderPass;
class Scene;
class Ground;
class PositionalLight;
class Skybox;

struct Transform
{
	vec3 scale = vec3(1.0f);
	vec3 translation = vec3(0.0f);
	quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
};

struct CachedTransform
{
	mat4 world_transform;
	mat4 normal_transform;
};

struct CachedSkinTransform
{
	std::vector<mat4> bone_world_transforms;
	std::vector<mat4> bone_normal_transforms;
};

struct BoundedComponent : ComponentBase
{
	const AABB *aabb;
};

struct UnboundedComponent : ComponentBase
{
};

struct BackgroundComponent : ComponentBase
{
};

struct EnvironmentComponent : ComponentBase
{
	FogParameters fog;
};

struct SkyboxComponent : ComponentBase
{
	Skybox *skybox;
};

struct IBLComponent : ComponentBase
{
	std::string reflection_path;
	std::string irradiance_path;
	float intensity;
};

struct RenderableComponent : ComponentBase
{
	AbstractRenderableHandle renderable;
};

struct CameraComponent : ComponentBase
{
	Camera camera;
};

struct RenderPassCreator
{
	virtual ~RenderPassCreator() = default;
	virtual void add_render_passes(RenderGraph &graph) = 0;
	virtual void set_base_renderer(Renderer *forward_renderer, Renderer *deferred_renderer, Renderer *depth_renderer) = 0;
	virtual void set_base_render_context(const RenderContext *context) = 0;
	virtual void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target) = 0;
	virtual void setup_render_pass_resources(RenderGraph &graph) = 0;
	virtual void set_scene(Scene *scene) = 0;
};

struct RenderPassSinkComponent : ComponentBase
{
};

struct CullPlaneComponent : ComponentBase
{
	vec4 plane;
};

struct GroundComponent : ComponentBase
{
	Ground *ground = nullptr;
};

struct RenderPassComponent : ComponentBase
{
	RenderPassCreator *creator = nullptr;
};

struct PerFrameRefreshableTransform
{
	virtual ~PerFrameRefreshableTransform() = default;
	virtual void refresh(RenderContext &context, const CachedSpatialTransformComponent *transform) = 0;
};

struct PerFrameRefreshable
{
	virtual ~PerFrameRefreshable() = default;
	virtual void refresh(RenderContext &context) = 0;
};

struct PerFrameUpdateTransformComponent : ComponentBase
{
	PerFrameRefreshableTransform *refresh = nullptr;
};

struct PerFrameUpdateComponent : ComponentBase
{
	PerFrameRefreshable *refresh = nullptr;
};

struct CachedSpatialTransformComponent : ComponentBase
{
	AABB world_aabb;
	CachedTransform *transform = nullptr;
	CachedSkinTransform *skin_transform = nullptr;
};

struct CachedTransformComponent : ComponentBase
{
	CachedTransform *transform = nullptr;
};

struct CachedSpatialTransformTimestampComponent : ComponentBase
{
	uint32_t last_timestamp = ~0u;
	const uint32_t *current_timestamp = nullptr;
};

struct OpaqueComponent : ComponentBase
{
};

struct TransparentComponent : ComponentBase
{
};

struct PositionalLightComponent : ComponentBase
{
	PositionalLight *light;
};

struct DirectionalLightComponent : ComponentBase
{
	vec3 color;
	vec3 direction;
};

struct AmbientLightComponent : ComponentBase
{
	vec3 color;
};

struct CastsStaticShadowComponent : ComponentBase
{
};

struct CastsDynamicShadowComponent : ComponentBase
{
};

}