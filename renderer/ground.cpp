#include "ground.hpp"
#include "vulkan_events.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "render_context.hpp"

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
	const Vulkan::ImageView *base_color;
	const Vulkan::ImageView *lod_map;
	const Vulkan::ImageView *type_map;

	mat4 push[2];

	vec4 lods;
	vec2 offsets;
	vec2 inv_heightmap_size;
	vec2 tiling_factor;
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
	//cmd.set_wireframe(true);
	cmd.set_primitive_restart(true);

	cmd.set_index_buffer(*patch.ibo, 0, VK_INDEX_TYPE_UINT16);
	cmd.set_vertex_binding(0, *patch.vbo, 0, sizeof(GroundVertex), VK_VERTEX_INPUT_RATE_VERTEX);
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(GroundVertex, pos));
	cmd.set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GroundVertex, weights));

	cmd.set_texture(2, 0, *patch.heights, cmd.get_device().get_stock_sampler(StockSampler::LinearWrap));
	cmd.set_texture(2, 1, *patch.normals, cmd.get_device().get_stock_sampler(StockSampler::TrilinearWrap));
	cmd.set_texture(2, 2, *patch.lod_map, cmd.get_device().get_stock_sampler(StockSampler::LinearClamp));
	cmd.set_texture(2, 3, *patch.base_color, cmd.get_device().get_stock_sampler(StockSampler::TrilinearWrap));
	cmd.set_texture(2, 4, *patch.type_map, cmd.get_device().get_stock_sampler(StockSampler::LinearClamp));

	auto *data = static_cast<GroundData *>(cmd.allocate_constant_data(3, 1, sizeof(GroundData)));
	data->inv_heightmap_size = patch.inv_heightmap_size;
	data->uv_shift = vec2(0.0f);
	data->uv_tiling_scale = patch.tiling_factor;

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

const float Ground::max_lod = 5.0f;
const unsigned Ground::base_patch_size = 64;

void GroundPatch::set_scale(vec2 offset, vec2 size)
{
	this->offset = offset;
	this->size = size;
	aabb = AABB(vec3(offset.x, -1.0f, offset.y), vec3(size.x + offset.x, 1.0f, size.y + offset.y));
}

GroundPatch::GroundPatch(Util::IntrusivePtr<Ground> ground)
	: ground(ground)
{
}

void GroundPatch::refresh(RenderContext &context, const CachedSpatialTransformComponent *transform)
{
	vec3 center = transform->world_aabb.get_center();
	const auto &camera_pos = context.get_render_parameters().camera_position;
	vec3 diff = center - camera_pos;
	float dist_log2 = 0.5f * glm::log2(dot(diff, diff) + 0.001f);
	*lod = clamp(dist_log2 - 3.0f, 0.0f, ground->max_lod);
}

void GroundPatch::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
                                  RenderQueue &queue) const
{
	ground->get_render_info(context, transform, queue, *this);
}

Ground::Ground(unsigned size, const TerrainInfo &info)
	: size(size), info(info)
{
	assert(size % base_patch_size == 0);
	num_patches_x = size / base_patch_size;
	num_patches_z = size / base_patch_size;
	patch_lods.resize(num_patches_x * num_patches_z);

	EventManager::get_global().register_latch_handler(DeviceCreatedEvent::type_id,
                                                      &Ground::on_device_created,
                                                      &Ground::on_device_destroyed,
                                                      this);
}

void Ground::on_device_created(const Event &e)
{
	auto &device = e.as<DeviceCreatedEvent>().get_device();
	heights = device.get_texture_manager().request_texture(info.heightmap);
	normals = device.get_texture_manager().request_texture(info.normalmap);
	base_color = device.get_texture_manager().request_texture(info.base_color);
	type_map = device.get_texture_manager().request_texture(info.typemap);
	build_buffers(device);

	ImageCreateInfo info = {};
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.domain = ImageDomain::Physical;
	info.width = num_patches_x;
	info.height = num_patches_z;
	info.levels = 1;
	info.format = VK_FORMAT_R8_UNORM;
	info.type = VK_IMAGE_TYPE_2D;
	info.depth = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	lod_map = device.create_image(info, nullptr);
}

void Ground::build_lod(Device &device, unsigned size, unsigned stride)
{
	unsigned size_1 = size + 1;
	vector<GroundVertex> vertices;
	vertices.reserve(size_1 * size_1);
	vector<uint16_t> indices;
	indices.reserve(size * (2 * size_1 + 1));

	unsigned half_size = base_patch_size >> 1;

	for (unsigned y = 0; y <= base_patch_size; y += stride)
	{
		for (unsigned x = 0; x <= base_patch_size; x += stride)
		{
			GroundVertex v = {};
			v.pos[0] = uint8_t(x);
			v.pos[1] = uint8_t(y);
			v.pos[2] = uint8_t(x < half_size);
			v.pos[3] = uint8_t(y < half_size);

			if (x == 0)
				v.weights[0] = 255;
			else if (x == base_patch_size)
				v.weights[1] = 255;
			else if (y == 0)
				v.weights[2] = 255;
			else if (y == base_patch_size)
				v.weights[3] = 255;

			vertices.push_back(v);
		}
	}

	unsigned slices = size;
	for (unsigned slice = 0; slice < slices; slice++)
	{
		unsigned base = slice * size_1;
		for (unsigned x = 0; x <= size; x++)
		{
			indices.push_back(base + x);
			indices.push_back(base + size_1 + x);
		}
		indices.push_back(0xffffu);
	}

	BufferCreateInfo info = {};
	info.size = vertices.size() * sizeof(GroundVertex);
	info.domain = BufferDomain::Device;
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	LOD lod;
	lod.vbo = device.create_buffer(info, vertices.data());

	info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	info.size = indices.size() * sizeof(uint16_t);
	lod.ibo = device.create_buffer(info, indices.data());
	lod.count = indices.size();

	quad_lod.push_back(lod);
}

void Ground::build_buffers(Device &device)
{
	unsigned size = base_patch_size;
	unsigned stride = 1;
	while (size >= 2)
	{
		build_lod(device, size, stride);
		size >>= 1;
		stride <<= 1;
	}
}

void Ground::on_device_destroyed(const Event &)
{
	heights = nullptr;
	normals = nullptr;
	base_color = nullptr;
	type_map = nullptr;
	quad_lod.clear();
	lod_map.reset();
}

void Ground::get_render_info(const RenderContext &context, const CachedSpatialTransformComponent *transform,
                             RenderQueue &queue, const GroundPatch &ground_patch) const
{
	auto &patch = queue.emplace<PatchInfo>(Queue::Opaque);
	patch.render = RenderFunctions::ground_patch_render;
	patch.push[0] = transform->transform->world_transform;

	// The normalmaps are generated with the reference that neighbor pixels are 1 unit apart.
	// However, the base mesh [0, size) is squashed to [0, 1] size in X/Z direction.
	// We compensate for this scaling by doing the inverse transposed normal matrix properly here.
	patch.push[1] = transform->transform->normal_transform * glm::scale(vec3(size, 1.0f, size));

	patch.lods = vec4(
		*ground_patch.nx->lod,
		*ground_patch.px->lod,
		*ground_patch.nz->lod,
		*ground_patch.pz->lod);
	patch.inner_lod = *ground_patch.lod;
	patch.lods = max(vec4(patch.inner_lod), patch.lods);

	int base_lod = int(patch.inner_lod);
	patch.vbo = quad_lod[base_lod].vbo.get();
	patch.ibo = quad_lod[base_lod].ibo.get();
	patch.count = quad_lod[base_lod].count;

	patch.program = queue.get_shader_suites()[ecast(RenderableType::Ground)].get_program(DrawPipeline::Opaque,
	                                                                                     MESH_ATTRIBUTE_POSITION_BIT,
	                                                                                     MATERIAL_TEXTURE_BASE_COLOR_BIT).get();

	auto heightmap = heights->get_image();
	auto normal = normals->get_image();
	auto base_color_image = base_color->get_image();
	auto typemap_image = type_map->get_image();
	patch.heights = &heightmap->get_view();
	patch.normals = &normal->get_view();
	patch.base_color = &base_color_image->get_view();
	patch.lod_map = &lod_map->get_view();
	patch.type_map = &typemap_image->get_view();
	patch.offsets = ground_patch.offset * vec2(size);
	patch.inv_heightmap_size = vec2(1.0f / size);
	patch.tiling_factor = tiling_factor;

	Util::Hasher hasher;
	hasher.pointer(patch.program);
	patch.sorting_key = RenderInfo::get_sort_key(context, Queue::Opaque, hasher.get(), transform->world_aabb.get_center());

	hasher.u64(heightmap->get_cookie());
	hasher.u64(normal->get_cookie());
	hasher.u64(base_color_image->get_cookie());
	hasher.u64(typemap_image->get_cookie());
	hasher.u64(lod_map->get_cookie());
	hasher.s32(base_lod);

	// Allow promotion to push constant for transforms.
	// We'll instance a lot of patches belonging to the same ground.
	hasher.pointer(transform->transform);

	patch.instance_key = hasher.get();
}

void Ground::refresh(RenderContext &context)
{
	auto &device = context.get_device();
	auto cmd = device.request_command_buffer();

	cmd->image_barrier(*lod_map, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                   VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
	lod_map->set_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	uint8_t *data = static_cast<uint8_t *>(cmd->update_image(*lod_map));

	const auto quantize = [](float v) -> uint8_t {
		return uint8_t(clamp(round(v * 32.0f), 0.0f, 255.0f));
	};

	for (auto lod : patch_lods)
		*data++ = quantize(lod);

	cmd->image_barrier(*lod_map, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	                   VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	lod_map->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	device.submit(cmd);
}

Ground::Handles Ground::add_to_scene(Scene &scene, unsigned size, float tiling_factor, const TerrainInfo &info)
{
	Handles handles;

	handles.node = scene.create_node();
	handles.entity = scene.create_entity();

	auto ground = make_handle<Ground>(size, info);
	ground->set_tiling_factor(vec2(tiling_factor));
	auto *update_component = handles.entity->allocate_component<PerFrameUpdateComponent>();
	update_component->refresh = ground.get();

	handles.ground = ground.get();

	vec2 inv_patches = vec2(1.0f / ground->get_num_patches_x(), 1.0f / ground->get_num_patches_z());

	vector<GroundPatch *> patches;
	patches.reserve(ground->get_num_patches_x() * ground->get_num_patches_z());

	for (unsigned z = 0; z < ground->get_num_patches_z(); z++)
	{
		for (unsigned x = 0; x < ground->get_num_patches_x(); x++)
		{
			auto patch = make_abstract_handle<AbstractRenderable, GroundPatch>(ground);
			auto *p = static_cast<GroundPatch *>(patch.get());
			p->set_scale(vec2(x, z) * inv_patches, inv_patches);
			p->set_lod_pointer(ground->get_lod_pointer(x, z));
			auto patch_entity = scene.create_renderable(patch, handles.node.get());
			auto *transforms = patch_entity->allocate_component<PerFrameUpdateTransformComponent>();
			transforms->refresh = p;
			patches.push_back(p);
		}
	}

	int num_x = int(ground->get_num_patches_x());
	int num_z = int(ground->get_num_patches_z());

	// Set up neighbors.
	for (int z = 0; z < num_z; z++)
	{
		for (int x = 0; x < num_x; x++)
		{
			auto *nx = patches[z * num_x + glm::max(x - 1, 0)];
			auto *px = patches[z * num_x + glm::min(x + 1, num_x - 1)];
			auto *nz = patches[glm::max(z - 1, 0) * num_x + x];
			auto *pz = patches[glm::min(z + 1, num_z - 1) * num_x + x];
			patches[z * num_x + x]->set_neighbors(nx, px, nz, pz);
		}
	}

	return handles;
}
}
