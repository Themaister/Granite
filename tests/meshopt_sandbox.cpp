#include "logging.hpp"
#include <vector>
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

static void decode_mesh_setup_buffers(
		std::vector<uint32_t> &out_index_buffer, std::vector<uint32_t> &out_u32_stream,
		const MeshView &mesh)
{
	assert(mesh.format_header->stream_count > 1);

	out_index_buffer.clear();
	out_u32_stream.clear();
	out_index_buffer.resize(mesh.total_primitives * 3);
	out_u32_stream.resize(mesh.total_vertices * (mesh.format_header->stream_count - 1));
}

static void decode_mesh_index_buffer(std::vector<uint32_t> &out_index_buffer, const MeshView &mesh)
{
	out_index_buffer.clear();
	out_index_buffer.reserve(mesh.total_primitives * 3);

	for (uint32_t meshlet_index = 0; meshlet_index < mesh.format_header->meshlet_count; meshlet_index++)
	{
		auto &meshlet = mesh.headers[meshlet_index];
		auto &stream = mesh.streams[meshlet_index * mesh.format_header->stream_count + int(StreamType::Primitive)];
		const auto *pdata = mesh.payload + stream.offset_in_b128;

		for (uint32_t i = 0; i < meshlet.num_primitives; i += 32, pdata += 4)
		{
			auto p0 = pdata[0];
			auto p1 = pdata[1];
			auto p2 = pdata[2];
			auto p3 = pdata[3];

			for (uint32_t j = 0; j + i < meshlet.num_primitives && j < 32; j++)
			{
				uint32_t v = 0;
				v |= ((p0.words[0] >> j) & 1u) << 0u;
				v |= ((p0.words[1] >> j) & 1u) << 1u;
				v |= ((p0.words[2] >> j) & 1u) << 2u;
				v |= ((p0.words[3] >> j) & 1u) << 3u;

				v |= ((p1.words[0] >> j) & 1u) << 8u;
				v |= ((p1.words[1] >> j) & 1u) << 9u;
				v |= ((p1.words[2] >> j) & 1u) << 10u;
				v |= ((p1.words[3] >> j) & 1u) << 11u;

				v |= ((p2.words[0] >> j) & 1u) << 16u;
				v |= ((p2.words[1] >> j) & 1u) << 17u;
				v |= ((p2.words[2] >> j) & 1u) << 18u;
				v |= ((p2.words[3] >> j) & 1u) << 19u;

				v |= ((p3.words[0] >> j) & 1u) << 4u;
				v |= ((p3.words[1] >> j) & 1u) << 12u;
				v |= ((p3.words[2] >> j) & 1u) << 20u;

				v += stream.base_value_or_vertex_offset[i] * 0x010101u;

				out_index_buffer.push_back(v);
			}
		}
	}
}

#if 0
static void decode_mesh_gpu(
		Vulkan::Device &dev,
		std::vector<uint32_t> &out_index_buffer, std::vector<uint32_t> &out_u32_stream,
		const MeshView &mesh)
{
	decode_mesh_setup_buffers(out_index_buffer, out_u32_stream, mesh);

	Vulkan::BufferCreateInfo buf_info = {};
	buf_info.domain = Vulkan::BufferDomain::LinkedDeviceHost;
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buf_info.size = mesh.format_header->payload_size_words * sizeof(uint32_t);
	auto payload_buffer = dev.create_buffer(buf_info, mesh.payload);

	buf_info.size = out_index_buffer.size() * sizeof(uint32_t);
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buf_info.domain = Vulkan::BufferDomain::CachedHost;
	auto readback_decoded_index_buffer = dev.create_buffer(buf_info);

	buf_info.size = out_u32_stream.size() * sizeof(uint32_t);
	buf_info.domain = Vulkan::BufferDomain::CachedHost;
	auto readback_decoded_u32_buffer = dev.create_buffer(buf_info);

	bool has_renderdoc = Vulkan::Device::init_renderdoc_capture();
	if (has_renderdoc)
		dev.begin_renderdoc_capture();

	auto cmd = dev.request_command_buffer();

	DecodeInfo info = {};
	info.ibo = readback_decoded_index_buffer.get();
	info.streams[0] = readback_decoded_u32_buffer.get();
	info.target_style = mesh.format_header->style;
	info.payload = payload_buffer.get();
	info.flags = DECODE_MODE_RAW_PAYLOAD;

	decode_mesh(*cmd, info, mesh);
	cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
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
#endif

int main(int argc, char *argv[])
{
	if (argc != 2)
		return EXIT_FAILURE;

#if 0
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

	return 0;
}