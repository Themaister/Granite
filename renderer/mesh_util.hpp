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

	void get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform, RenderQueue &queue) const override;

private:
	std::string bg_path;
	Vulkan::Texture *texture = nullptr;
	void on_device_created(const Event &event);

	void on_device_destroyed(const Event &event);
};
}