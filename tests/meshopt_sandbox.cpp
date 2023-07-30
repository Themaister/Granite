#include "logging.hpp"
#include <stdint.h>
#include <vector>
#include "math.hpp"
#include "device.hpp"
#include "context.hpp"
#include "muglm/muglm_impl.hpp"
#include <unordered_map>
#include "bitops.hpp"
#include "gltf.hpp"
#include "global_managers_init.hpp"
#include "meshlet.hpp"
#include <assert.h>
#include <algorithm>
using namespace Granite;
using namespace Granite::Meshlet;

static void decode_mesh_setup_buffers(
		std::vector<uint32_t> &out_index_buffer, std::vector<uint32_t> &out_u32_stream, const CombinedMesh &mesh)
{
	assert(mesh.stream_count > 1);

	unsigned index_count = 0;
	unsigned attr_count = 0;

	for (auto &meshlet : mesh.meshlets)
	{
		index_count += (meshlet.num_primitives_minus_1 + 1) * 3;
		attr_count += meshlet.num_attributes_minus_1 + 1;
	}

	out_index_buffer.clear();
	out_u32_stream.clear();
	out_index_buffer.resize(index_count);
	out_u32_stream.resize(attr_count * (mesh.stream_count - 1));
}

static void decode_mesh(std::vector<uint32_t> &out_index_buffer, std::vector<uint32_t> &out_u32_stream,
                        const std::vector<uint32_t> &payload, const CombinedMesh &mesh)
{
	decode_mesh_setup_buffers(out_index_buffer, out_u32_stream, mesh);
	out_index_buffer.clear();
	const unsigned u32_stride = mesh.stream_count - 1;

	for (auto &meshlet : mesh.meshlets)
	{
		for (unsigned stream_index = 0; stream_index < mesh.stream_count; stream_index++)
		{
			auto &stream = meshlet.u32_streams[stream_index];
			const uint32_t *pdata = payload.data() + mesh.data_stream_offset_u32 + stream.offset_from_base_u32;

			u8vec4 deltas[MaxElements] = {};
			const u16vec4 base_predictor = u16vec4(
					stream.predictor[0], stream.predictor[1],
					stream.predictor[2], stream.predictor[3]);
			const u16vec4 linear_predictor = u16vec4(
					stream.predictor[4], stream.predictor[5],
					stream.predictor[6], stream.predictor[7]);
			const u8vec4 initial_value =
					u8vec4(u16vec2(stream.predictor[8], stream.predictor[9]).xxyy() >> u16vec4(0, 8, 0, 8));

			for (unsigned chunk = 0; chunk < (MaxElements / 32); chunk++)
			{
				auto bits_per_u8 = (uvec4(stream.bitplane_meta[chunk]) >> uvec4(0, 4, 8, 12)) & 0xfu;
				uvec4 bitplanes[8] = {};

				for (unsigned comp = 0; comp < 4; comp++)
				{
					for (unsigned bit = 0; bit < bits_per_u8[comp]; bit++)
						bitplanes[bit][comp] = *pdata++;

					// Sign-extend.

					unsigned bit_count = bits_per_u8[comp];
					if (bit_count)
						for (unsigned bit = bit_count; bit < 8; bit++)
							bitplanes[bit][comp] = bitplanes[bit_count - 1][comp];
				}

				for (unsigned i = 0; i < 32; i++)
				{
					for (uint32_t bit = 0; bit < 8; bit++)
						deltas[chunk * 32 + i] |= u8vec4(((bitplanes[bit] >> i) & 1u) << bit);
				}
			}

			// Apply predictors.
			deltas[0] += initial_value;
			for (unsigned i = 0; i < MaxElements; i++)
				deltas[i] += u8vec4((base_predictor + linear_predictor * u16vec4(i)) >> u16vec4(8));

			// Resolve deltas.
			for (unsigned i = 1; i < MaxElements; i++)
				deltas[i] += deltas[i - 1];

			if (stream_index == 0)
			{
				// Index decode.
				unsigned num_primitives = meshlet.num_primitives_minus_1 + 1;
				for (unsigned i = 0; i < num_primitives; i++)
					for (unsigned j = 0; j < 3; j++)
						out_index_buffer.push_back(deltas[i][j] + meshlet.base_vertex_offset);
			}
			else
			{
				// Attributes.
				unsigned num_attributes = meshlet.num_attributes_minus_1 + 1;
				auto *out_attr = out_u32_stream.data() + meshlet.base_vertex_offset * u32_stride + (stream_index - 1);
				for (unsigned i = 0; i < num_attributes; i++, out_attr += u32_stride)
					memcpy(out_attr, deltas[i].data, sizeof(*out_attr));
			}
		}
	}
}

static void decode_mesh_gpu(
		Vulkan::Device &dev,
		std::vector<uint32_t> &out_index_buffer, std::vector<uint32_t> &out_u32_stream,
		const std::vector<uint32_t> &payload, const CombinedMesh &mesh)
{
	decode_mesh_setup_buffers(out_index_buffer, out_u32_stream, mesh);
	const uint32_t u32_stride = mesh.stream_count - 1;

	Vulkan::BufferCreateInfo buf_info = {};
	buf_info.domain = Vulkan::BufferDomain::LinkedDeviceHost;
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	std::vector<MetadataGPU> meshlet_metas;
	meshlet_metas.reserve(mesh.meshlets.size());
	for (auto &meshlet : mesh.meshlets)
		meshlet_metas.push_back(meshlet);
	buf_info.size = mesh.meshlets.size() * sizeof(MetadataGPU);
	auto meshlet_meta_buffer = dev.create_buffer(buf_info, meshlet_metas.data());

	std::vector<Stream> meshlet_streams;
	meshlet_streams.reserve(mesh.meshlets.size() * mesh.stream_count);
	for (auto &meshlet : mesh.meshlets)
		for (unsigned i = 0; i < mesh.stream_count; i++)
			meshlet_streams.push_back(meshlet.u32_streams[i]);
	buf_info.size = meshlet_streams.size() * sizeof(Stream);
	auto meshlet_stream_buffer = dev.create_buffer(buf_info, meshlet_streams.data());

	buf_info.size = payload.size() * sizeof(uint32_t);
	if (buf_info.size == 0)
		buf_info.size = 4;
	auto payload_buffer = dev.create_buffer(buf_info, payload.empty() ? nullptr : payload.data());

	buf_info.size = out_index_buffer.size() * sizeof(uint32_t);
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
	                 VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buf_info.domain = Vulkan::BufferDomain::Device;
	auto decoded_index_buffer = dev.create_buffer(buf_info);
	buf_info.domain = Vulkan::BufferDomain::CachedHost;
	auto readback_decoded_index_buffer = dev.create_buffer(buf_info);

	buf_info.size = out_u32_stream.size() * sizeof(uint32_t);
	buf_info.domain = Vulkan::BufferDomain::Device;
	auto decoded_u32_buffer = dev.create_buffer(buf_info);
	buf_info.domain = Vulkan::BufferDomain::CachedHost;
	auto readback_decoded_u32_buffer = dev.create_buffer(buf_info);

	std::vector<uvec2> output_offset_strides;
	output_offset_strides.reserve(mesh.meshlets.size() * mesh.stream_count);

	uint32_t index_count = 0;
	for (auto &meshlet : mesh.meshlets)
	{
		output_offset_strides.emplace_back(index_count, 0);
		index_count += meshlet.num_primitives_minus_1 + 1;
		for (uint32_t i = 1; i < mesh.stream_count; i++)
			output_offset_strides.emplace_back(meshlet.base_vertex_offset * u32_stride + (i - 1), u32_stride);
	}

	buf_info.domain = Vulkan::BufferDomain::LinkedDeviceHost;
	buf_info.size = output_offset_strides.size() * sizeof(uvec2);
	auto output_offset_strides_buffer = dev.create_buffer(buf_info, output_offset_strides.data());

	bool has_renderdoc = Vulkan::Device::init_renderdoc_capture();
	if (has_renderdoc)
		dev.begin_renderdoc_capture();

	auto cmd = dev.request_command_buffer();
	cmd->set_program("builtin://shaders/decode/meshlet_decode.comp");
	cmd->enable_subgroup_size_control(true);
	cmd->set_subgroup_size_log2(true, 5, 5);
	cmd->set_storage_buffer(0, 0, *meshlet_meta_buffer);
	cmd->set_storage_buffer(0, 1, *meshlet_stream_buffer);
	cmd->set_storage_buffer(0, 2, *decoded_u32_buffer);
	cmd->set_storage_buffer(0, 3, *decoded_index_buffer);
	cmd->set_storage_buffer(0, 4, *payload_buffer);
	cmd->set_storage_buffer(0, 5, *output_offset_strides_buffer);
	cmd->set_specialization_constant_mask(1);
	cmd->set_specialization_constant(0, mesh.stream_count);
	cmd->dispatch(uint32_t(mesh.meshlets.size()), 1, 1);

	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_READ_BIT);

	cmd->copy_buffer(*readback_decoded_index_buffer, *decoded_index_buffer);
	cmd->copy_buffer(*readback_decoded_u32_buffer, *decoded_u32_buffer);
	cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	dev.submit(cmd);

	dev.wait_idle();

	if (has_renderdoc)
		dev.end_renderdoc_capture();

	memcpy(out_index_buffer.data(),
	       dev.map_host_buffer(*readback_decoded_index_buffer, Vulkan::MEMORY_ACCESS_READ_BIT),
	       out_index_buffer.size() * sizeof(uint32_t));

	memcpy(out_u32_stream.data(),
	       dev.map_host_buffer(*readback_decoded_u32_buffer, Vulkan::MEMORY_ACCESS_READ_BIT),
	       out_u32_stream.size() * sizeof(uint32_t));
}

static bool validate_mesh_decode(const std::vector<uint32_t> &decoded_index_buffer,
                                 const std::vector<uint32_t> &decoded_u32_stream,
                                 const std::vector<uint32_t> &reference_index_buffer,
                                 const std::vector<uint32_t> &reference_u32_stream, unsigned u32_stride)
{
	std::vector<uint32_t> decoded_output;
	std::vector<uint32_t> reference_output;

	if (decoded_index_buffer.size() != reference_index_buffer.size())
		return false;

	size_t count = decoded_index_buffer.size();

	decoded_output.reserve(count * u32_stride);
	reference_output.reserve(count * u32_stride);
	for (size_t i = 0; i < count; i++)
	{
		uint32_t decoded_index = decoded_index_buffer[i];
		decoded_output.insert(decoded_output.end(),
		                      decoded_u32_stream.data() + decoded_index * u32_stride,
		                      decoded_u32_stream.data() + (decoded_index + 1) * u32_stride);

		uint32_t reference_index = reference_index_buffer[i];
		reference_output.insert(reference_output.end(),
		                        reference_u32_stream.data() + reference_index * u32_stride,
		                        reference_u32_stream.data() + (reference_index + 1) * u32_stride);
	}

	for (size_t i = 0; i < count; i++)
	{
		for (unsigned j = 0; j < u32_stride; j++)
		{
			uint32_t decoded_value = decoded_output[i * u32_stride + j];
			uint32_t reference_value = reference_output[i * u32_stride + j];
			if (decoded_value != reference_value)
			{
				LOGI("Error in index %zu (prim %zu), word %u, expected %x, got %x.\n",
				     i, i / 3, j, reference_value, decoded_value);
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

	GLTF::Parser parser(argv[1]);

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
	dev.init_frame_contexts(4);

#if 1
	{
		Meshlet::export_mesh_to_meshlet("/tmp/export.mesh", parser.get_meshes().front());
	}
#endif

#if 0
	{
		std::vector<uvec3> index_buffer;
		std::vector<Meshlet> meshlets;
		std::vector<meshopt_Bounds> bounds;

		for (auto &mesh : parser.get_meshes())
		{
			if (mesh.count < 60000)
				continue;
			if (!convert_meshlets(meshlets, bounds, index_buffer, mesh))
				return EXIT_FAILURE;
			break;
		}
	}
#endif

#if 0
	LOGI("=== Test ====\n");
	{
		std::vector<uint32_t> out_payload_buffer;
		const std::vector<uint32_t> index_buffer = {
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
		};
		const std::vector<uint32_t> attr_buffer = {
			9, 11, 4, 4, 2, 9,
			9, 7, 4, 29, 2, 9,
			9, 7, 4, 29, 2, 9,
			9, 7, 4, 29, 2, 9,
		};
		MeshMetadata encoded_mesh;
		const uint32_t u32_stride = 2;

		encode_mesh(out_payload_buffer, encoded_mesh,
		            index_buffer.data(), index_buffer.size() / 3,
		            attr_buffer.data(), u32_stride);

		LOGI("Encoded payload size = %zu bytes.\n", out_payload_buffer.size() * sizeof(uint32_t));
		LOGI("u32 stride = %u\n", u32_stride);

		std::vector<uint32_t> decoded_index_buffer;
		std::vector<uint32_t> decoded_u32_stream;
		std::vector<uint32_t> gpu_decoded_index_buffer;
		std::vector<uint32_t> gpu_decoded_u32_stream;
		decode_mesh(decoded_index_buffer, decoded_u32_stream, out_payload_buffer, encoded_mesh);

		if (!validate_mesh_decode(decoded_index_buffer, decoded_u32_stream, index_buffer, attr_buffer, u32_stride))
		{
			LOGE("Failed to validate mesh.\n");
			return EXIT_FAILURE;
		}

		decode_mesh_gpu(dev, gpu_decoded_index_buffer, gpu_decoded_u32_stream, out_payload_buffer, encoded_mesh);
		if (!validate_mesh_decode(gpu_decoded_index_buffer, gpu_decoded_u32_stream, decoded_index_buffer, decoded_u32_stream, u32_stride))
		{
			LOGE("Failed to validate GPU decoded mesh.\n");
			return EXIT_FAILURE;
		}
	}
	LOGI("===============\n");
#endif

#if 0
	for (auto &mesh : parser.get_meshes())
	{
		unsigned u32_stride = (mesh.position_stride + mesh.attribute_stride) / sizeof(uint32_t);

		if (mesh.indices.empty() || mesh.primitive_restart || mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
		{
			LOGI("Unexpected mesh.\n");
			continue;
		}

		std::vector<uint32_t> index_buffer;
		std::vector<uint32_t> attr_buffer;
		size_t vertex_count = mesh.positions.size() / mesh.position_stride;
		attr_buffer.resize(u32_stride * vertex_count);
		index_buffer.resize(mesh.count);

		if (mesh.index_type == VK_INDEX_TYPE_UINT32)
		{
			memcpy(index_buffer.data(), mesh.indices.data(), mesh.count * sizeof(uint32_t));
		}
		else if (mesh.index_type == VK_INDEX_TYPE_UINT16)
		{
			auto *indices = reinterpret_cast<const uint16_t *>(mesh.indices.data());
			for (unsigned i = 0; i < mesh.count; i++)
				index_buffer[i] = indices[i];
		}
		else if (mesh.index_type == VK_INDEX_TYPE_UINT8_EXT)
		{
			auto *indices = reinterpret_cast<const uint8_t *>(mesh.indices.data());
			for (unsigned i = 0; i < mesh.count; i++)
				index_buffer[i] = indices[i];
		}
		else
			continue;

		LOGI("=== Testing mesh ===\n");

		for (size_t i = 0; i < vertex_count; i++)
		{
			memcpy(attr_buffer.data() + u32_stride * i, mesh.positions.data() + i * mesh.position_stride, mesh.position_stride);
			memcpy(attr_buffer.data() + u32_stride * i + mesh.position_stride / sizeof(uint32_t),
				   mesh.attributes.data() + i * mesh.attribute_stride, mesh.attribute_stride);
		}

		LOGI("Mesh payload size = %zu bytes.\n", (index_buffer.size() + attr_buffer.size()) * sizeof(uint32_t));

		std::vector<uint32_t> optimized_index_buffer(index_buffer.size());
		meshopt_optimizeVertexCache(optimized_index_buffer.data(), index_buffer.data(), mesh.count, vertex_count);

		std::vector<uint32_t> out_payload_buffer;
		MeshMetadata encoded_mesh;
		encode_mesh(out_payload_buffer, encoded_mesh,
		            optimized_index_buffer.data(), optimized_index_buffer.size() / 3,
		            attr_buffer.data(), u32_stride);

		unsigned prim_offset = 0;
		unsigned meshlet_index = 0;
		for (auto &meshlet : encoded_mesh.meshlets)
		{
			LOGI("Meshlet #%u (%u prims, %u attrs), offset %u.\n",
				 meshlet_index, meshlet.num_primitives_minus_1 + 1, meshlet.num_attributes_minus_1 + 1, prim_offset);
			prim_offset += meshlet.num_primitives_minus_1 + 1;
			meshlet_index++;
		}

		LOGI("Encoded payload size = %zu bytes.\n", out_payload_buffer.size() * sizeof(uint32_t));
		LOGI("u32 stride = %u\n", u32_stride);

		std::vector<uint32_t> decoded_index_buffer;
		std::vector<uint32_t> decoded_u32_stream;
		std::vector<uint32_t> gpu_decoded_index_buffer;
		std::vector<uint32_t> gpu_decoded_u32_stream;
		decode_mesh(decoded_index_buffer, decoded_u32_stream, out_payload_buffer, encoded_mesh);

		if (!validate_mesh_decode(decoded_index_buffer, decoded_u32_stream, optimized_index_buffer, attr_buffer, u32_stride))
		{
			LOGE("Failed to validate mesh.\n");
			return EXIT_FAILURE;
		}

		decode_mesh_gpu(dev, gpu_decoded_index_buffer, gpu_decoded_u32_stream, out_payload_buffer, encoded_mesh);
		if (!validate_mesh_decode(gpu_decoded_index_buffer, gpu_decoded_u32_stream, decoded_index_buffer, decoded_u32_stream, u32_stride))
		{
			LOGE("Failed to validate GPU decoded mesh.\n");
			return EXIT_FAILURE;
		}

		LOGI("=====================\n");
	}
#endif

	return 0;
}