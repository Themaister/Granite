#include "logging.hpp"
#include <vector>
#include <algorithm>
#include "math.hpp"
#include "device.hpp"
#include "context.hpp"
#include "muglm/muglm_impl.hpp"
#include "gltf.hpp"
#include "global_managers_init.hpp"
#include "meshlet_export.hpp"
#include "meshlet.hpp"
#include <assert.h>
using namespace Granite;
using namespace Vulkan::Meshlet;

static void decode_mesh_index_buffer(std::vector<uvec3> &out_index_buffer, const MeshView &mesh, uint32_t meshlet_index)
{
	auto &meshlet = mesh.headers[meshlet_index];
	auto &stream = mesh.streams[meshlet_index * mesh.format_header->stream_count + int(StreamType::Primitive)];
	const auto *pdata = mesh.payload + stream.offset_in_b128;

	for (uint32_t chunk_index = 0; chunk_index < meshlet.num_chunks; chunk_index++)
	{
		auto p0 = pdata[0];
		auto p1 = pdata[1];
		auto p2 = pdata[2];
		auto p3 = pdata[3];

		pdata += 4;

		uint32_t num_primitives_for_chunk = stream.u.offsets[chunk_index + 1].prim_offset -
		                                    stream.u.offsets[chunk_index].prim_offset;

		for (uint32_t i = 0; i < num_primitives_for_chunk; i++)
		{
			uint32_t v = 0;
			v |= ((p0.words[0] >> i) & 1u) << 0u;
			v |= ((p0.words[1] >> i) & 1u) << 1u;
			v |= ((p0.words[2] >> i) & 1u) << 2u;
			v |= ((p0.words[3] >> i) & 1u) << 3u;

			v |= ((p1.words[0] >> i) & 1u) << 8u;
			v |= ((p1.words[1] >> i) & 1u) << 9u;
			v |= ((p1.words[2] >> i) & 1u) << 10u;
			v |= ((p1.words[3] >> i) & 1u) << 11u;

			v |= ((p2.words[0] >> i) & 1u) << 16u;
			v |= ((p2.words[1] >> i) & 1u) << 17u;
			v |= ((p2.words[2] >> i) & 1u) << 18u;
			v |= ((p2.words[3] >> i) & 1u) << 19u;

			v |= ((p3.words[0] >> i) & 1u) << 4u;
			v |= ((p3.words[1] >> i) & 1u) << 12u;
			v |= ((p3.words[2] >> i) & 1u) << 20u;

			v += stream.u.offsets[chunk_index].attr_offset * 0x010101u;

			uint32_t x = v & 0xffu;
			uint32_t y = (v >> 8u) & 0xffu;
			uint32_t z = (v >> 16u) & 0xffu;

			out_index_buffer.push_back(uvec3(x, y, z) + meshlet.base_vertex_offset);
		}
	}
}

template <int Components, typename T>
static void decode_bitfield_block_16(T *block, const PayloadB128 *&pdata, unsigned config)
{
	unsigned bit_offset = 0;

	for (int mask = 4; mask; mask >>= 1)
	{
		if (config & mask)
		{
			const uint32_t *words = &pdata->words[0];
			int bits = mask * 2;

			for (uint32_t i = 0; i < ElementsPerChunk; i++)
			{
				T &d = block[i];
				for (int c = 0; c < Components; c++)
				{
					for (int b = 0; b < bits; b++)
					{
						int word = c * bits + b;
						d[c] |= ((words[word] >> i) & 1u) << (bit_offset + b);
					}
				}
			}

			int num_words = (bits * Components + 3) / 4;
			pdata += num_words;
			bit_offset += bits;
		}
	}
}

static void decode_attribute_buffer(std::vector<vec3> &out_positions, const MeshView &mesh, uint32_t meshlet_index, StreamType type)
{
	auto &meshlet = mesh.headers[meshlet_index];
	auto &index_stream = mesh.streams[meshlet_index * mesh.format_header->stream_count + int(StreamType::Primitive)];
	auto &stream = mesh.streams[meshlet_index * mesh.format_header->stream_count + int(type)];
	const auto *pdata = mesh.payload + stream.offset_in_b128;

	for (uint32_t chunk = 0; chunk < meshlet.num_chunks; chunk++)
	{
		u16vec3 positions[ElementsPerChunk]{};
		unsigned config = (stream.bit_plane_config >> (4 * chunk)) & 0xf;

		if (config == 8)
		{
			for (uint32_t i = 0; i < ElementsPerChunk; i++)
			{
				memcpy(positions[i].data, &pdata[i / 4].words[i % 4], 2 * sizeof(uint16_t));
				memcpy(&positions[i].z,
				       reinterpret_cast<const char *>(&pdata[8]) + sizeof(uint16_t) * i,
				       sizeof(uint16_t));
			}

			pdata += 12;
		}
		else
		{
			decode_bitfield_block_16<3>(positions, pdata, config);
		}

		u16vec3 base;
		memcpy(base.data, &stream.u.base_value[chunk], sizeof(uint16_t) * 2);
		memcpy(&base.z, reinterpret_cast<const char *>(&stream.u.base_value[NumChunks]) +
		                sizeof(uint16_t) * chunk,
		       sizeof(uint16_t));

		for (auto &p : positions)
			p += base;

		uint32_t num_attributes_for_chunk = index_stream.u.offsets[chunk + 1].attr_offset -
		                                    index_stream.u.offsets[chunk].attr_offset;

		for (uint32_t i = 0; i < num_attributes_for_chunk; i++)
		{
			vec3 float_pos = vec3(i16vec3(positions[i]));
			float_pos.x = ldexpf(float_pos.x, stream.aux);
			float_pos.y = ldexpf(float_pos.y, stream.aux);
			float_pos.z = ldexpf(float_pos.z, stream.aux);
			out_positions.push_back(float_pos);
		}
	}
}

static void decode_mesh(std::vector<uvec3> &out_index_buffer,
                        std::vector<vec3> &out_positions,
                        const MeshView &mesh)
{
	for (uint32_t meshlet_index = 0; meshlet_index < mesh.format_header->meshlet_count; meshlet_index++)
	{
		decode_mesh_index_buffer(out_index_buffer, mesh, meshlet_index);
		decode_attribute_buffer(out_positions, mesh, meshlet_index, StreamType::Position);
	}
}

static void decode_mesh_gpu(
		Vulkan::Device &dev,
		std::vector<uvec3> &out_index_buffer, std::vector<vec3> &out_pos_buffer,
		const MeshView &mesh)
{
	out_index_buffer.resize(mesh.total_primitives);
	out_pos_buffer.resize(mesh.total_vertices);

	Vulkan::BufferCreateInfo buf_info = {};
	buf_info.domain = Vulkan::BufferDomain::LinkedDeviceHost;
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buf_info.size = mesh.format_header->payload_size_b128 * sizeof(PayloadB128);
	auto payload_buffer = dev.create_buffer(buf_info, mesh.payload);

	buf_info.size = out_index_buffer.size() * sizeof(uvec3);
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buf_info.domain = Vulkan::BufferDomain::CachedHost;
	auto readback_decoded_index_buffer = dev.create_buffer(buf_info);

	buf_info.size = out_pos_buffer.size() * sizeof(vec3);
	buf_info.domain = Vulkan::BufferDomain::CachedHost;
	auto readback_decoded_pos_buffer = dev.create_buffer(buf_info);

	bool has_renderdoc = Vulkan::Device::init_renderdoc_capture();
	if (has_renderdoc)
		dev.begin_renderdoc_capture();

	auto cmd = dev.request_command_buffer();

	DecodeInfo info = {};
	info.ibo = readback_decoded_index_buffer.get();
	info.streams[0] = readback_decoded_pos_buffer.get();
	info.target_style = mesh.format_header->style;
	info.payload = payload_buffer.get();
	info.flags = DECODE_MODE_UNROLLED_MESH;

	decode_mesh(*cmd, info, mesh);
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	dev.submit(cmd);
	dev.wait_idle();

	if (has_renderdoc)
		dev.end_renderdoc_capture();

	memcpy(out_index_buffer.data(),
	       dev.map_host_buffer(*readback_decoded_index_buffer, Vulkan::MEMORY_ACCESS_READ_BIT),
	       out_index_buffer.size() * sizeof(uvec3));

	memcpy(out_pos_buffer.data(),
	       dev.map_host_buffer(*readback_decoded_pos_buffer, Vulkan::MEMORY_ACCESS_READ_BIT),
	       out_pos_buffer.size() * sizeof(vec3));
}

static void build_reference_mesh(std::vector<uvec3> &indices, std::vector<vec3> &positions)
{
	for (unsigned i = 0; i < 256; i++)
	{
#if 1
		vec3 p;
		p.x = -40.0f + float(i);
		p.y = float(i);
		p.z = -30.0f + float(i);

		if (i == 8)
			p.y = 20000.0f;
#else
		vec3 p = vec3(-40.0f + float(i));
#endif
		positions.push_back(p);
	}

	for (unsigned i = 0; i < 254; i++)
		indices.push_back(uvec3(i, i + 1, i + 2));
}

static bool validate_mesh(std::vector<uvec3> &reference_indices,
                          std::vector<vec3> &reference_positions,
                          std::vector<uvec3> &decoded_indices,
                          std::vector<vec3> &decoded_positions,
						  bool need_sorting)
{
	if (reference_indices.size() != decoded_indices.size())
	{
		LOGE("Mismatch in index buffer size.\n");
		return false;
	}

	if (need_sorting)
	{
		std::sort(reference_indices.begin(), reference_indices.end(), [&](const uvec3 &a, const uvec3 &b)
		{
			float za = reference_positions[a.z].z;
			float zb = reference_positions[b.z].z;
			return za < zb;
		});

		std::sort(decoded_indices.begin(), decoded_indices.end(), [&](const uvec3 &a, const uvec3 &b)
		{
			float za = decoded_positions[a.z].z;
			float zb = decoded_positions[b.z].z;
			return za < zb;
		});
	}

	for (size_t i = 0, n = decoded_indices.size(); i < n; i++)
	{
		uvec3 ref_i = reference_indices[i];
		uvec3 decode_i = decoded_indices[i];

		for (int c = 0; c < 3; c++)
		{
			vec3 ref_pos = reference_positions[ref_i[c]];
			vec3 decode_pos = decoded_positions[decode_i[c]];
			if (any(notEqual(ref_pos, decode_pos)))
			{
				LOGE("Mismatch in primitive %zu, c = %d.\n", i, c);
				return false;
			}
		}
	}

	return true;
}

int main(int argc, char *argv[])
{
	if (argc != 2)
		return EXIT_FAILURE;

	Global::init(Global::MANAGER_FEATURE_FILESYSTEM_BIT);
	Filesystem::setup_default_filesystem(GRANITE_FILESYSTEM(), ASSET_DIRECTORY);

	SceneFormats::Mesh mesh;

	std::vector<uvec3> reference_indices;
	std::vector<vec3> reference_positions;
	build_reference_mesh(reference_indices, reference_positions);

	mesh.index_type = VK_INDEX_TYPE_UINT32;
	mesh.count = 3 * reference_indices.size();
	mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	mesh.indices.resize(mesh.count * sizeof(uint32_t));
	memcpy(mesh.indices.data(), reference_indices.data(), mesh.count * sizeof(uint32_t));

	mesh.attribute_layout[int(MeshAttribute::Position)].format = VK_FORMAT_R32G32B32_SFLOAT;
	mesh.position_stride = sizeof(vec3);
	mesh.positions.resize(reference_positions.size() * sizeof(vec3));
	memcpy(mesh.positions.data(), reference_positions.data(), reference_positions.size() * sizeof(vec3));

	if (!Meshlet::export_mesh_to_meshlet("/tmp/export.msh2", std::move(mesh), MeshStyle::Wireframe))
		return EXIT_FAILURE;

	auto file = GRANITE_FILESYSTEM()->open("/tmp/export.msh2", FileMode::ReadOnly);
	if (!file)
		return EXIT_FAILURE;

	auto mapped = file->map();
	if (!mapped)
		return EXIT_FAILURE;

	std::vector<uvec3> decoded_index_buffer;
	std::vector<vec3> decoded_positions;
	auto view = create_mesh_view(*mapped);
	decode_mesh(decoded_index_buffer, decoded_positions, view);

	Vulkan::Context ctx;
	Vulkan::Device dev;
	if (!Vulkan::Context::init_loader(nullptr))
		return EXIT_FAILURE;

	Vulkan::Context::SystemHandles handles;
	handles.filesystem = GRANITE_FILESYSTEM();
	ctx.set_system_handles(handles);
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;
	dev.set_context(ctx);

	std::vector<uvec3> gpu_index_buffer;
	std::vector<vec3> gpu_positions;
	decode_mesh_gpu(dev, gpu_index_buffer, gpu_positions, view);

	if (!validate_mesh(decoded_index_buffer, decoded_positions,
	                   gpu_index_buffer, gpu_positions, false))
		return EXIT_FAILURE;

	if (!validate_mesh(reference_indices, reference_positions,
	                   decoded_index_buffer, decoded_positions, true))
		return EXIT_FAILURE;

	return 0;
#if 0
	GLTF::Parser parser(argv[1]);


	dev.init_frame_contexts(4);

	auto mesh = parser.get_meshes().front();

	if (!Meshlet::export_mesh_to_meshlet("export.msh1",
	                                     mesh, MeshStyle::Textured))
	{
		return EXIT_FAILURE;
	}

	auto file = GRANITE_FILESYSTEM()->open("export.msh1", FileMode::ReadOnly);
	if (!file)
		return EXIT_FAILURE;

	auto mapped = file->map();
	if (!mapped)
		return EXIT_FAILURE;

	auto view = create_mesh_view(*mapped);

	std::vector<uint32_t> reference_index_buffer;
	std::vector<uint32_t> reference_attributes;
	std::vector<uint32_t> gpu_index_buffer;
	std::vector<uint32_t> gpu_attributes;

	decode_mesh(reference_index_buffer, reference_attributes, view);
	decode_mesh_gpu(dev, gpu_index_buffer, gpu_attributes, view);

	if (!validate_mesh_decode(gpu_index_buffer, gpu_attributes,
	                          reference_index_buffer, reference_attributes,
	                          view.format_header->u32_stream_count - 1))
	{
		return EXIT_FAILURE;
	}

	{
		LOGI("Total primitives: %u\n", view.total_primitives);
		LOGI("Total vertices: %u\n", view.total_vertices);
		LOGI("Payload size: %llu bytes.\n", static_cast<unsigned long long>(view.format_header->payload_size_words * sizeof(uint32_t)));

		unsigned long long uncompressed_mesh_size =
				view.total_primitives * sizeof(uint32_t) * 3 +
				view.total_vertices * (view.format_header->u32_stream_count - 1) * sizeof(uint32_t);
		unsigned long long uncompressed_payload_size =
				view.total_primitives * sizeof(uint32_t) +
				view.total_vertices * (view.format_header->u32_stream_count - 1) * sizeof(uint32_t);
		LOGI("Uncompressed mesh size: %llu bytes.\n", uncompressed_mesh_size);
		LOGI("Uncompressed payload size: %llu bytes.\n", uncompressed_payload_size);
	}

	{
		file = GRANITE_FILESYSTEM()->open("export.bin", FileMode::WriteOnly);
		mapped = file->map_write((reference_index_buffer.size() + reference_attributes.size()) * sizeof(uint32_t));
		auto *ptr = mapped->mutable_data<uint32_t>();
		memcpy(ptr, reference_index_buffer.data(), reference_index_buffer.size() * sizeof(uint32_t));
		memcpy(ptr + reference_index_buffer.size(), reference_attributes.data(), reference_attributes.size() * sizeof(uint32_t));
	}
#endif
}