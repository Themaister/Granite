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

class TexturePlane : public AbstractRenderable, public EventHandler
{
public:
	TexturePlane(const std::string &normal);

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
	                     RenderQueue &queue) const override;

	void set_reflection_texture(const Vulkan::ImageView *view)
	{
		reflection = view;
	}

	void set_refraction_texture(const Vulkan::ImageView *view)
	{
		refraction = view;
	}

	void set_position(vec3 position)
	{
		this->position = position;
	}

	void set_dpdx(vec3 dpdx)
	{
		this->dpdx = dpdx;
	}

	void set_dpdy(vec3 dpdy)
	{
		this->dpdy = dpdy;
	}

	void set_normal(vec3 normal)
	{
		this->normal = normal;
	}

private:
	std::string normal_path;
	const Vulkan::ImageView *reflection = nullptr;
	const Vulkan::ImageView *refraction = nullptr;
	Vulkan::Texture *normalmap = nullptr;
	vec3 position;
	vec3 normal;
	vec3 dpdx;
	vec3 dpdy;
	double elapsed = 0.0f;
	void on_device_created(const Event &event);
	void on_device_destroyed(const Event &event);
	bool on_frame_time(const Event &e);
};
}