#include "ground.hpp"
#include "vulkan_events.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "image.hpp"

using namespace Vulkan;
using namespace std;
using namespace Util;

namespace Granite
{

struct PatchInfo : RenderInfo
{
	Program *program;

	const Vulkan::Buffer *vbo;
	const Vulkan::Buffer *ibo;
	unsigned count;

	const Vulkan::ImageView *heights;
	const Vulkan::ImageView *normals;
	const Vulkan::ImageView *lod_map;

	mat4 push[2];

	vec4 lods;
	vec2 offsets;
	float inner_lod;
};

struct GroundVertex
{
	uint8_t pos[4];
	uint8_t weights[4];
};

struct GroundData
{
	vec2 inv_heightmap_size;
	vec2 uv_shift;
	vec2 uv_tiling_scale;
};

struct PatchData
{
    vec2 Offset;
	float InnerLOD;
	float Padding;
    vec4 LODs;
};

namespace RenderFunctions
{
static void ground_patch_render(Vulkan::CommandBuffer &cmd, const RenderInfo **infos, unsigned instances)
{
	auto &patch = *static_cast<const PatchInfo *>(infos[0]);

	cmd.set_program(*patch.program);
	cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	cmd.set_wireframe(true);
	cmd.set_primitive_restart(true);

	cmd.set_index_buffer(*patch.ibo, 0, VK_INDEX_TYPE_UINT16);
	cmd.set_vertex_binding(0, *patch.vbo, sizeof(GroundVertex), VK_VERTEX_INPUT_RATE_VERTEX);
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(GroundVertex, pos));
	cmd.set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GroundVertex, weights));

	cmd.set_texture(2, 0, *patch.heights, cmd.get_device().get_stock_sampler(StockSampler::LinearWrap));
	cmd.set_texture(2, 1, *patch.normals, cmd.get_device().get_stock_sampler(StockSampler::TrilinearWrap));
	cmd.set_texture(2, 2, *patch.lod_map, cmd.get_device().get_stock_sampler(StockSampler::LinearWrap));

	auto *data = static_cast<GroundData *>(cmd.allocate_constant_data(2, 4, sizeof(GroundData)));
	data->inv_heightmap_size = vec2(1.0f / patch.heights->get_image().get_create_info().width,
	                                1.0f / patch.heights->get_image().get_create_info().height);
	data->uv_shift = vec2(0.0f);
	data->uv_tiling_scale = vec2(16.0f);

	cmd.push_constants(patch.push, 0, sizeof(patch.push));

	for (unsigned i = 0; i < instances; i += 64)
	{
		unsigned to_render = std::min(instances - i, 64u);

		auto *patches = static_cast<PatchData *>(cmd.allocate_constant_data(3, 0, sizeof(PatchData) * to_render));
		for (unsigned j = 0; j < to_render; j++)
		{
			auto &patch = *static_cast<const PatchInfo *>(infos[i + j]);
			patches->LODs = patch.lods;
			patches->InnerLOD = patch.inner_lod;
			patches->Offset = patch.offsets;
			patches++;
		}

		cmd.draw_indexed(patch.count, to_render);
	}
}
}
void GroundPatch::set_scale(vec2 base, vec2 offset)
{
	this->base = base;
	this->offset = offset;
	aabb = AABB(vec3(base, -1.0f), vec3(base + offset, 2.0f));
}

GroundPatch::GroundPatch(Util::IntrusivePtr<Ground> ground)
	: ground(ground)
{
}

void GroundPatch::refresh(RenderContext &context, const CachedSpatialTransformComponent *transform)
{

}

void GroundPatch::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
                                  RenderQueue &queue) const
{
	ground->get_render_info(context, transform, queue, *this);
}

Ground::Ground(const string &heightmap, const string &normalmap)
	: heightmap_path(heightmap), normalmap_path(normalmap)
{
	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
                                                      &Ground::on_device_created,
                                                      &Ground::on_device_destroyed,
                                                      this);
}

void Ground::on_device_created(const Event &e)
{
	auto &device = e.as<DeviceCreatedEvent>().get_device();
	heights = device.get_texture_manager().request_texture(heightmap_path);
	normals = device.get_texture_manager().request_texture(normalmap_path);
}

void Ground::on_device_destroyed(const Event &)
{
	heights = nullptr;
	normals = nullptr;
	quad_lod.clear();
	lod_map.reset();
}

void Ground::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
                             RenderQueue &queue, const GroundPatch &ground_patch) const
{
	auto &patch = queue.emplace<PatchInfo>(Queue::Opaque);
	patch.render = RenderFunctions::ground_patch_render;
	patch.push[0] = transform->transform->world_transform;
	patch.push[1] = transform->transform->normal_transform;

	patch.lods = vec4(
		ground_patch.nx->lod,
		ground_patch.px->lod,
		ground_patch.nz->lod,
		ground_patch.pz->lod);
	patch.inner_lod = ground_patch.lod;

	float lod = ground_patch.lod;
	for (unsigned i = 0; i < 4; i++)
		lod = glm::min(lod, patch.lods[i]);

	int base_lod = int(lod);
	patch.vbo = quad_lod[base_lod].vbo.get();
	patch.ibo = quad_lod[base_lod].ibo.get();
	patch.count = quad_lod[base_lod].count;

	patch.program = queue.get_shader_suites()[ecast(RenderableType::Ground)].get_program(DrawPipeline::Opaque,
	                                                                                     MESH_ATTRIBUTE_POSITION_BIT,
	                                                                                     MATERIAL_TEXTURE_BASE_COLOR_BIT).get();

	auto heightmap = heights->get_image();
	auto normal = normals->get_image();
	patch.heights = &heightmap->get_view();
	patch.normals = &normal->get_view();
	patch.lod_map = &lod_map->get_view();
	patch.offsets = ground_patch.offset;

	Util::Hasher hasher;
	hasher.pointer(patch.program);
	patch.sorting_key = RenderInfo::get_sort_key(context, Queue::Opaque, hasher.get(), transform->world_aabb.get_center());

	hasher.u64(heightmap->get_cookie());
	hasher.u64(normal->get_cookie());
	hasher.s32(base_lod);

	// Allow promotion to push constant for transforms.
	// We'll instance a lot of patches belonging to the same ground.
	hasher.pointer(transform);

	patch.instance_key = hasher.get();
}

void Ground::refresh(RenderContext &context)
{
}
}