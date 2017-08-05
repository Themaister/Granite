/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "mesh.hpp"
#include "vulkan_events.hpp"
#include "importers.hpp"
#include "render_components.hpp"
#include "render_context.hpp"

namespace Granite
{
class ImportedMesh : public StaticMesh, public EventHandler
{
public:
	ImportedMesh(const Importer::Mesh &mesh, const Importer::MaterialInfo &info);

private:
	Importer::Mesh mesh;
	Importer::MaterialInfo info;

	void on_device_created(const Event &event);

	void on_device_destroyed(const Event &event);
};

class ImportedSkinnedMesh : public SkinnedMesh, public EventHandler
{
public:
	ImportedSkinnedMesh(const Importer::Mesh &mesh, const Importer::MaterialInfo &info);

private:
	Importer::Mesh mesh;
	Importer::MaterialInfo info;

	void on_device_created(const Event &event);

	void on_device_destroyed(const Event &event);
};

class CubeMesh : public StaticMesh, public EventHandler
{
public:
	CubeMesh();

private:
	void on_device_created(const Event &event);

	void on_device_destroyed(const Event &event);
};

class Skybox : public AbstractRenderable, public EventHandler
{
public:
	Skybox(std::string bg_path);

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
	                     RenderQueue &queue) const override;

private:
	std::string bg_path;
	Vulkan::Texture *texture = nullptr;

	void on_device_created(const Event &event);
	void on_device_destroyed(const Event &event);
};

class TexturePlane : public AbstractRenderable, public EventHandler, public RenderPassCreator
{
public:
	TexturePlane(const std::string &normal);

	void set_reflection_name(const std::string &name)
	{
		need_reflection = !name.empty();
		reflection_name = name;
	}

	void set_refraction_name(const std::string &name)
	{
		need_refraction = !name.empty();
		refraction_name = name;
	}

	void set_resolution_scale(float x, float y)
	{
		scale_x = x;
		scale_y = y;
	}

	vec4 get_plane() const
	{
		return vec4(normal, -dot(normal, position));
	}

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
	                     RenderQueue &queue) const override;

	void set_plane(const vec3 &position, const vec3 &normal, const vec3 &up, float extent_up, float extent_across);
	void set_base_emissive(const vec3 &color)
	{
		base_emissive = color;
	}

	void set_zfar(float zfar);

private:
	std::string normal_path;
	const Vulkan::ImageView *reflection = nullptr;
	const Vulkan::ImageView *refraction = nullptr;
	Vulkan::Texture *normalmap = nullptr;

	vec3 position;
	vec3 normal;
	vec3 up;
	vec3 dpdx;
	vec3 dpdy;
	vec3 base_emissive;
	float rad_up = 0.0f;
	float rad_x = 0.0f;
	float zfar = 100.0f;
	float scale_x = 1.0f;
	float scale_y = 1.0f;

	double elapsed = 0.0f;
	void on_device_created(const Event &event);
	void on_device_destroyed(const Event &event);
	bool on_frame_time(const Event &e);

	std::string reflection_name;
	std::string refraction_name;

	Renderer *renderer = nullptr;
	const RenderContext *base_context = nullptr;
	RenderContext context;
	Scene *scene = nullptr;
	VisibilityList visible;

	bool need_reflection = false;
	bool need_refraction = false;

	enum Type
	{
		Reflection,
		Refraction
	};
	void add_render_pass(RenderGraph &graph, Type type);

	void add_render_passes(RenderGraph &graph) override;
	void set_base_renderer(Renderer *renderer) override;
	void set_base_render_context(const RenderContext *context) override;
	void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void set_scene(Scene *scene) override;
	RendererType get_renderer_type() override;

	void render_main_pass(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view);
};
}