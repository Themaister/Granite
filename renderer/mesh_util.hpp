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

#include "mesh.hpp"
#include "application_wsi_events.hpp"
#include "scene_formats.hpp"
#include "render_components.hpp"
#include "render_context.hpp"

namespace Granite
{
class FrameTickEvent;
class ImportedMesh : public StaticMesh, public EventHandler
{
public:
	ImportedMesh(const SceneFormats::Mesh &mesh, const SceneFormats::MaterialInfo &info);

	const SceneFormats::Mesh &get_mesh() const;
	const SceneFormats::MaterialInfo &get_material_info() const;

private:
	SceneFormats::Mesh mesh;
	SceneFormats::MaterialInfo info;

	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);
};

class ImportedSkinnedMesh : public SkinnedMesh, public EventHandler
{
public:
	ImportedSkinnedMesh(const SceneFormats::Mesh &mesh, const SceneFormats::MaterialInfo &info);

	const SceneFormats::Mesh &get_mesh() const;
	const SceneFormats::MaterialInfo &get_material_info() const;

private:
	SceneFormats::Mesh mesh;
	SceneFormats::MaterialInfo info;

	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);
};

template <typename StaticMesh = ImportedMesh, typename SkinnedMesh = ImportedSkinnedMesh>
inline AbstractRenderableHandle create_imported_mesh(const SceneFormats::Mesh &mesh,
                                                     const SceneFormats::MaterialInfo *materials)
{
	SceneFormats::MaterialInfo default_material;
	default_material.uniform_base_color = vec4(0.3f, 1.0f, 0.3f, 1.0f);
	default_material.uniform_metallic = 0.0f;
	default_material.uniform_roughness = 1.0f;
	AbstractRenderableHandle renderable;

	bool skinned = mesh.attribute_layout[Util::ecast(MeshAttribute::BoneIndex)].format != VK_FORMAT_UNDEFINED;
	if (skinned)
	{
		if (mesh.has_material)
		{
			renderable = Util::make_handle<SkinnedMesh>(mesh,
			                                            materials[mesh.material_index]);
		}
		else
			renderable = Util::make_handle<SkinnedMesh>(mesh, default_material);
	}
	else
	{
		if (mesh.has_material)
		{
			renderable = Util::make_handle<StaticMesh>(mesh,
			                                           materials[mesh.material_index]);
		}
		else
			renderable = Util::make_handle<StaticMesh>(mesh, default_material);
	}
	return renderable;
}

class CubeMesh : public StaticMesh, public EventHandler
{
public:
	CubeMesh();
	static SceneFormats::Mesh build_plain_mesh();

private:
	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);
};

struct GeneratedMeshData
{
	struct Attribute
	{
		vec3 normal;
		vec2 uv;
	};

	std::vector<vec3> positions;
	std::vector<Attribute> attributes;
	std::vector<uint16_t> indices;

	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	bool primitive_restart = false;
	bool has_uvs = false;
};
GeneratedMeshData create_sphere_mesh(unsigned density);
GeneratedMeshData create_cone_mesh(unsigned density, float height, float radius);
GeneratedMeshData create_cylinder_mesh(unsigned density, float height, float radius);
GeneratedMeshData create_capsule_mesh(unsigned density, float height, float radius);

class GeneratedMesh : public StaticMesh
{
protected:
	void setup_from_generated_mesh(Vulkan::Device &device, const GeneratedMeshData &generated);
};

class SphereMesh : public GeneratedMesh, public EventHandler
{
public:
	SphereMesh(unsigned density = 16);

private:
	unsigned density;
	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);
};

class ConeMesh : public GeneratedMesh, public EventHandler
{
public:
	ConeMesh(unsigned density, float height, float radius);

private:
	unsigned density;
	float height;
	float radius;
	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);
};

class CylinderMesh : public GeneratedMesh, public EventHandler
{
public:
	CylinderMesh(unsigned density, float height, float radius);

private:
	unsigned density;
	float height;
	float radius;
	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);
};

class CapsuleMesh : public GeneratedMesh, public EventHandler
{
public:
	CapsuleMesh(unsigned density, float height, float radius);

private:
	unsigned density;
	float height;
	float radius;
	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);
};

class Skybox : public AbstractRenderable, public EventHandler
{
public:
	Skybox(std::string bg_path = "", bool latlon = false);
	void set_image(Vulkan::ImageHandle skybox);
	void set_image(Vulkan::Texture *skybox);

	void get_render_info(const RenderContext &context, const RenderInfoComponent *transform,
	                     RenderQueue &queue) const override;

	void set_color_mod(const vec3 &color_)
	{
		color = color_;
	}

private:
	Vulkan::Device *device = nullptr;
	std::string bg_path;
	vec3 color = vec3(1.0f);
	Vulkan::Texture *texture = nullptr;
	Vulkan::ImageHandle image;

	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);

	bool is_latlon = true;
};

class SkyCylinder : public AbstractRenderable, public EventHandler
{
public:
	SkyCylinder(std::string bg_path);

	void get_render_info(const RenderContext &context, const RenderInfoComponent *transform,
	                     RenderQueue &queue) const override;

	void set_color_mod(const vec3 &color_)
	{
		color = color_;
	}

	void set_xz_scale(float scale_)
	{
		scale = scale_;
	}

private:
	std::string bg_path;
	vec3 color = vec3(1.0f);
	float scale = 1.0f;
	Vulkan::Texture *texture = nullptr;

	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);

	Vulkan::BufferHandle vbo;
	Vulkan::BufferHandle ibo;
	Vulkan::SamplerHandle sampler;
	unsigned count = 0;
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

	void get_render_info(const RenderContext &context, const RenderInfoComponent *transform,
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
	RenderQueue internal_queue;

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
	void on_device_created(const Vulkan::DeviceCreatedEvent &event);
	void on_device_destroyed(const Vulkan::DeviceCreatedEvent &event);
	bool on_frame_time(const FrameTickEvent &e);

	std::string reflection_name;
	std::string refraction_name;

	const RendererSuite *renderer_suite = nullptr;
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
	void set_base_renderer(const RendererSuite *suite) override;
	void set_base_render_context(const RenderContext *context) override;
	void setup_render_pass_dependencies(RenderGraph &graph, RenderPass &target) override;
	void setup_render_pass_resources(RenderGraph &graph) override;
	void set_scene(Scene *scene) override;

	void render_main_pass(Vulkan::CommandBuffer &cmd, const mat4 &proj, const mat4 &view);
};
}
