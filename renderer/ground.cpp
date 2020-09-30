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

#include "ground.hpp"
#include "device.hpp"
#include "renderer.hpp"
#include "render_context.hpp"
#include "muglm/matrix_helper.hpp"
#include "transforms.hpp"

using namespace Vulkan;
using namespace std;
using namespace Util;

namespace Granite
{

struct PatchInstanceInfo
{
	vec4 lods;
	vec2 offsets;
	float inner_lod;
};

struct PatchInfo
{
	Program *program;

	const Vulkan::Buffer *vbo;
	const Vulkan::Buffer *ibo;
	unsigned count;

	const Vulkan::ImageView *heights;
	const Vulkan::ImageView *normals;
	const Vulkan::ImageView *occlusion;
	const Vulkan::ImageView *normals_fine;
	const Vulkan::ImageView *base_color;
	const Vulkan::ImageView *lod_map;
	const Vulkan::ImageView *type_map;

	mat4 push[2];

	vec2 inv_heightmap_size;
	vec2 tiling_factor;
	vec2 tangent_scale;
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
	vec2 tangent_scale;
	vec4 texture_info;
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
static void ground_patch_render(Vulkan::CommandBuffer &cmd, const RenderQueueData *infos, unsigned instances)
{
	auto &patch = *static_cast<const PatchInfo *>(infos->render_info);

	cmd.set_program(patch.program);
	cmd.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	//cmd.set_wireframe(true);
	cmd.set_primitive_restart(true);

	cmd.set_index_buffer(*patch.ibo, 0, VK_INDEX_TYPE_UINT16);
	cmd.set_vertex_binding(0, *patch.vbo, 0, sizeof(GroundVertex), VK_VERTEX_INPUT_RATE_VERTEX);
	cmd.set_vertex_attrib(0, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(GroundVertex, pos));
	cmd.set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GroundVertex, weights));

	cmd.set_texture(2, 0, *patch.heights, cmd.get_device().get_stock_sampler(StockSampler::LinearClamp));
	cmd.set_texture(2, 1, *patch.normals, cmd.get_device().get_stock_sampler(StockSampler::TrilinearClamp));
	cmd.set_texture(2, 2, *patch.occlusion, cmd.get_device().get_stock_sampler(StockSampler::LinearClamp));
	cmd.set_texture(2, 3, *patch.lod_map, cmd.get_device().get_stock_sampler(StockSampler::LinearClamp));
	cmd.set_texture(2, 4, *patch.base_color, cmd.get_device().get_stock_sampler(StockSampler::TrilinearWrap));
	cmd.set_texture(2, 5, *patch.type_map, cmd.get_device().get_stock_sampler(StockSampler::LinearClamp));
	cmd.set_texture(2, 6, *patch.normals_fine, cmd.get_device().get_stock_sampler(StockSampler::TrilinearWrap));

	auto *data = static_cast<GroundData *>(cmd.allocate_constant_data(3, 1, sizeof(GroundData)));
	data->inv_heightmap_size = patch.inv_heightmap_size;
	data->uv_shift = vec2(0.0f);
	data->uv_tiling_scale = patch.tiling_factor;
	data->tangent_scale = patch.tangent_scale;
	data->texture_info.x = float(patch.base_color->get_image().get_width(0));
	data->texture_info.y = float(patch.base_color->get_image().get_height(0));
	data->texture_info.z = 1.0f / float(patch.base_color->get_image().get_width(0));
	data->texture_info.w = 1.0f / float(patch.base_color->get_image().get_height(0));

	cmd.push_constants(patch.push, 0, sizeof(patch.push));

	for (unsigned i = 0; i < instances; i += 512)
	{
		unsigned to_render = std::min(instances - i, 512u);

		auto *patches = static_cast<PatchData *>(cmd.allocate_constant_data(3, 0, sizeof(PatchData) * to_render));
		for (unsigned j = 0; j < to_render; j++)
		{
			auto &patch_info = *static_cast<const PatchInstanceInfo *>(infos[i + j].instance_data);
			patches->LODs = patch_info.lods;
			patches->InnerLOD = patch_info.inner_lod;
			patches->Offset = patch_info.offsets;
			patches++;
		}

		cmd.draw_indexed(patch.count, to_render);
	}
}
}

void GroundPatch::set_bounds(vec3 offset_, vec3 size_)
{
	offset = offset_.xz();
	size = size_.xz();
	aabb = AABB(offset_, offset_ + size_);
}

GroundPatch::GroundPatch(Util::IntrusivePtr<Ground> ground_)
	: ground(std::move(ground_))
{
}

GroundPatch::~GroundPatch()
{
}

void GroundPatch::refresh(const RenderContext &context, const RenderInfoComponent *transform, TaskComposer &)
{
	vec3 center = transform->world_aabb.get_center();
	const auto &camera_pos = context.get_render_parameters().camera_position;
	vec3 diff = center - camera_pos;
	float dist_log2 = 0.5f * muglm::log2(dot(diff, diff) + 0.001f);
	*lod = clamp(dist_log2 + lod_bias + ground->get_base_lod_bias(), 0.0f, ground->get_info().max_lod);
}

void GroundPatch::get_render_info(const RenderContext &context, const RenderInfoComponent *transform,
                                  RenderQueue &queue) const
{
	ground->get_render_info(context, transform, queue, *this);
}

Ground::Ground(unsigned size_, const TerrainInfo &info_)
	: size(size_), info(info_)
{
	assert(size % info.base_patch_size == 0);
	num_patches_x = size / info.base_patch_size;
	num_patches_z = size / info.base_patch_size;
	patch_lods.resize(num_patches_x * num_patches_z);

	EVENT_MANAGER_REGISTER_LATCH(Ground, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void Ground::on_device_created(const DeviceCreatedEvent &created)
{
	auto &device = created.get_device();
	heights = device.get_texture_manager().request_texture(info.heightmap);
	normals = device.get_texture_manager().request_texture(info.normalmap);
	occlusion = device.get_texture_manager().request_texture(info.occlusionmap);
	normals_fine = device.get_texture_manager().request_texture(info.normalmap_fine);
	base_color = device.get_texture_manager().request_texture(info.base_color);
	type_map = device.get_texture_manager().request_texture(info.splatmap);
	build_buffers(device);

	ImageCreateInfo image_info = {};
	image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	image_info.domain = ImageDomain::Physical;
	image_info.width = num_patches_x;
	image_info.height = num_patches_z;
	image_info.levels = 1;
	image_info.format = VK_FORMAT_R16_SFLOAT;
	image_info.type = VK_IMAGE_TYPE_2D;
	image_info.depth = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	lod_map = device.create_image(image_info, nullptr);
}

void Ground::build_lod(Device &device, unsigned lod_size, unsigned stride)
{
	unsigned size_1 = lod_size + 1;
	vector<GroundVertex> vertices;
	vertices.reserve(size_1 * size_1);
	vector<uint16_t> indices;
	indices.reserve(lod_size * (2 * size_1 + 1));

	unsigned half_size = info.base_patch_size >> 1;

	for (unsigned y = 0; y <= info.base_patch_size; y += stride)
	{
		for (unsigned x = 0; x <= info.base_patch_size; x += stride)
		{
			GroundVertex v = {};
			v.pos[0] = uint8_t(x);
			v.pos[1] = uint8_t(y);
			v.pos[2] = uint8_t(x < half_size);
			v.pos[3] = uint8_t(y < half_size);

			if (x == 0)
				v.weights[0] = 255;
			else if (x == info.base_patch_size)
				v.weights[1] = 255;
			else if (y == 0)
				v.weights[2] = 255;
			else if (y == info.base_patch_size)
				v.weights[3] = 255;

			vertices.push_back(v);
		}
	}

	unsigned slices = lod_size;
	for (unsigned slice = 0; slice < slices; slice++)
	{
		unsigned base = slice * size_1;
		for (unsigned x = 0; x <= lod_size; x++)
		{
			indices.push_back(base + x);
			indices.push_back(base + size_1 + x);
		}
		indices.push_back(0xffffu);
	}

	BufferCreateInfo buffer_info = {};
	buffer_info.size = vertices.size() * sizeof(GroundVertex);
	buffer_info.domain = BufferDomain::Device;
	buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	LOD lod;
	lod.vbo = device.create_buffer(buffer_info, vertices.data());

	buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	buffer_info.size = indices.size() * sizeof(uint16_t);
	lod.ibo = device.create_buffer(buffer_info, indices.data());
	lod.count = indices.size();

	quad_lod.push_back(lod);
}

void Ground::build_buffers(Device &device)
{
	unsigned base_size = info.base_patch_size;
	unsigned stride = 1;
	while (base_size >= 2)
	{
		build_lod(device, base_size, stride);
		base_size >>= 1;
		stride <<= 1;
	}
}

void Ground::on_device_destroyed(const DeviceCreatedEvent &)
{
	heights = nullptr;
	normals = nullptr;
	occlusion = nullptr;
	normals_fine = nullptr;
	base_color = nullptr;
	type_map = nullptr;
	quad_lod.clear();
	lod_map.reset();
}

void Ground::get_render_info(const RenderContext &context, const RenderInfoComponent *transform,
                             RenderQueue &queue, const GroundPatch &ground_patch) const
{
	PatchInfo patch;
	patch.push[0] = transform->transform->world_transform;

	// The normalmaps are generated with the reference that neighbor pixels are certain length apart.
	// However, the base mesh [0, normal_size) is squashed to [0, 1] size in X/Z direction.
	// We compensate for this scaling by doing the inverse transposed normal matrix properly here.
	//patch.push[1] = transform->transform->normal_transform * scale(vec3(info.normal_size, 1.0f, info.normal_size));
	mat4 normal_transform;
	compute_normal_transform(normal_transform, transform->transform->world_transform);
	patch.push[1] = normal_transform * scale(vec3(info.normal_size, 1.0f, info.normal_size));

	// Find something concrete to put here.
	patch.tangent_scale = vec2(1.0f / 10.0f);

	auto *instance_data = queue.allocate_one<PatchInstanceInfo>();
	instance_data->lods = vec4(
		*ground_patch.nx->lod,
		*ground_patch.px->lod,
		*ground_patch.nz->lod,
		*ground_patch.pz->lod);
	instance_data->inner_lod = *ground_patch.lod;
	instance_data->lods = max(vec4(instance_data->inner_lod), instance_data->lods);
	instance_data->offsets = ground_patch.offset * vec2(size);

	int base_lod = int(instance_data->inner_lod);
	patch.vbo = quad_lod[base_lod].vbo.get();
	patch.ibo = quad_lod[base_lod].ibo.get();
	patch.count = quad_lod[base_lod].count;

	auto heightmap = heights->get_image();
	auto normal = normals->get_image();
	auto occlusionmap = occlusion->get_image();
	auto normal_fine = normals_fine->get_image();
	auto base_color_image = base_color->get_image();
	auto splatmap_image = type_map->get_image();
	patch.heights = &heightmap->get_view();
	patch.normals = &normal->get_view();
	patch.occlusion = &occlusionmap->get_view();
	patch.normals_fine = &normal_fine->get_view();
	patch.base_color = &base_color_image->get_view();
	patch.lod_map = &lod_map->get_view();
	patch.type_map = &splatmap_image->get_view();
	patch.inv_heightmap_size = vec2(1.0f / size);
	patch.tiling_factor = tiling_factor;

	Util::Hasher hasher;
	hasher.string("ground");
	auto pipe_hash = hasher.get();
	hasher.s32(base_lod);
	hasher.s32(info.bandlimited_pixel);
	auto sorting_key = RenderInfo::get_sort_key(context, Queue::Opaque, pipe_hash, hasher.get(),
	                                            transform->world_aabb.get_center(),
	                                            StaticLayer::Last);

	hasher.u64(heightmap->get_cookie());
	hasher.u64(normal->get_cookie());
	hasher.u64(normal_fine->get_cookie());
	hasher.u64(occlusionmap->get_cookie());
	hasher.u64(base_color_image->get_cookie());
	hasher.u64(splatmap_image->get_cookie());
	hasher.u64(lod_map->get_cookie());

	// Allow promotion to push constant for transforms.
	// We'll instance a lot of patches belonging to the same ground.
	hasher.pointer(transform->transform);

	auto instance_key = hasher.get();

	auto *patch_data = queue.push<PatchInfo>(Queue::Opaque, instance_key, sorting_key,
	                                         RenderFunctions::ground_patch_render,
	                                         instance_data);

	if (patch_data)
	{
		uint32_t flags = 0;
		if (info.bandlimited_pixel)
			flags |= 1u << 0;

		patch.program = queue.get_shader_suites()[ecast(RenderableType::Ground)].get_program(DrawPipeline::Opaque,
		                                                                                     MESH_ATTRIBUTE_POSITION_BIT,
		                                                                                     MATERIAL_TEXTURE_BASE_COLOR_BIT,
		                                                                                     flags);

		*patch_data = patch;
	}
}

void Ground::refresh(const RenderContext &context, TaskComposer &)
{
	auto &device = context.get_device();
	auto cmd = device.request_command_buffer();

	cmd->image_barrier(*lod_map, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                   VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
	uint16_t *data = static_cast<uint16_t *>(cmd->update_image(*lod_map));

	const auto quantize = [](float v) -> uint16_t {
		return floatToHalf(v);
	};

	for (auto lod : patch_lods)
		*data++ = quantize(lod);

	cmd->image_barrier(*lod_map, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	                   VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	device.submit(cmd);
}

Ground::Handles Ground::add_to_scene(Scene &scene, unsigned size, float tiling_factor, const TerrainInfo &info)
{
	Handles handles;

	handles.node = scene.create_node();
	handles.entity = scene.create_entity();

	auto ground = make_handle<Ground>(size, info);
	ground->set_tiling_factor(vec2(tiling_factor));

	auto *ground_component = handles.entity->allocate_component<GroundComponent>();
	ground_component->ground = ground.get();

	auto *update_component = handles.entity->allocate_component<PerFrameUpdateComponent>();
	update_component->refresh = ground.get();

	auto *cached_transform = handles.entity->allocate_component<RenderInfoComponent>();
	cached_transform->transform = &handles.node->cached_transform;
	cached_transform->skin_transform = nullptr;

	handles.ground = ground.get();

	vec2 inv_patches = vec2(1.0f / ground->get_num_patches_x(), 1.0f / ground->get_num_patches_z());

	vector<GroundPatch *> patches;
	patches.reserve(ground->get_num_patches_x() * ground->get_num_patches_z());

	if (!info.patch_lod_bias.empty() && info.patch_lod_bias.size() != (ground->get_num_patches_x() * ground->get_num_patches_z()))
		throw logic_error("Mismatch in number of patch lod biases and patches.");

	const float *patch_bias = info.patch_lod_bias.empty() ? nullptr : info.patch_lod_bias.data();
	const vec2 *patch_range = info.patch_range.empty() ? nullptr : info.patch_range.data();

	for (unsigned z = 0; z < ground->get_num_patches_z(); z++)
	{
		for (unsigned x = 0; x < ground->get_num_patches_x(); x++)
		{
			auto patch = make_handle<GroundPatch>(ground);

			float min_y = -1.0f;
			float max_y = 1.0f;
			if (patch_range)
			{
				min_y = patch_range->x;
				max_y = patch_range->y;
				patch_range++;
			}

			patch->set_bounds(vec3(x * inv_patches.x, min_y - 0.01f, z * inv_patches.y), vec3(inv_patches.x, max_y - min_y + 0.02f, inv_patches.y));

			patch->set_lod_pointer(ground->get_lod_pointer(x, z));
			auto patch_entity = scene.create_renderable(patch, handles.node.get());

			// TODO: Warpy patches shouldn't cast static shadow.
			patch_entity->free_component<CastsStaticShadowComponent>();

			auto *transforms = patch_entity->allocate_component<PerFrameUpdateTransformComponent>();
			transforms->refresh = patch.get();

			if (patch_bias)
			{
				patch->lod_bias = *patch_bias;
				patch_bias++;
			}

			patches.push_back(patch.get());
		}
	}

	int num_x = int(ground->get_num_patches_x());
	int num_z = int(ground->get_num_patches_z());

	// Set up neighbors.
	for (int z = 0; z < num_z; z++)
	{
		for (int x = 0; x < num_x; x++)
		{
			auto *nx = patches[z * num_x + muglm::max(x - 1, 0)];
			auto *px = patches[z * num_x + muglm::min(x + 1, num_x - 1)];
			auto *nz = patches[muglm::max(z - 1, 0) * num_x + x];
			auto *pz = patches[muglm::min(z + 1, num_z - 1) * num_x + x];
			patches[z * num_x + x]->set_neighbors(nx, px, nz, pz);
		}
	}

	return handles;
}
}
