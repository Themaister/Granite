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
#include "meshoptimizer.h"
#include "global_managers_init.hpp"
#include <assert.h>
#include <algorithm>
using namespace Granite;

static constexpr unsigned MaxStreams = 16;
static constexpr unsigned MaxU32Streams = 16;
static constexpr unsigned MaxElements = 256;
static constexpr unsigned MaxPrimitives = MaxElements;
static constexpr unsigned MaxVertices = MaxElements;

struct MeshletStream
{
	uint16_t predictor[4 * 2 + 2];
	uint32_t offset_from_base_u32;
	uint16_t bitplane_meta[MaxElements / 32];
};

struct MeshletMetadataGPU
{
	uint32_t base_vertex_offset;
	uint8_t num_primitives_minus_1;
	uint8_t num_attributes_minus_1;
	uint16_t reserved;
};

struct MeshletMetadata : MeshletMetadataGPU
{
	MeshletStream u32_streams[MaxU32Streams];
};

enum class StreamType : uint8_t
{
	Primitive, // R8G8B8X8_UINT
	PositionF16, // R16G16B16X16_FLOAT
};

struct StreamMeta
{
	StreamType type;
	uint8_t stream_index_component;
};

struct MeshMetadata
{
	uint32_t stream_count;
	uint32_t data_stream_offset_u32;
	uint32_t data_stream_size_u32;

	// Stream meta is used to configure the decode shader.
	StreamMeta stream_meta[MaxStreams];

	std::vector<MeshletMetadata> meshlets;
};

struct PrimitiveAnalysisResult
{
	uint32_t num_primitives;
	uint32_t num_vertices;
};

static PrimitiveAnalysisResult analyze_primitive_count(std::unordered_map<uint32_t, uint32_t> &vertex_remap,
													   const uint32_t *index_buffer, uint32_t max_num_primitives)
{
	PrimitiveAnalysisResult result = {};
	uint32_t vertex_count = 0;

	// We can reference a maximum of 256 vertices.
	vertex_remap.clear();

	for (uint32_t i = 0; i < max_num_primitives; i++)
	{
		uint32_t index0 = index_buffer[3 * i + 0];
		uint32_t index1 = index_buffer[3 * i + 1];
		uint32_t index2 = index_buffer[3 * i + 2];

		vertex_count = uint32_t(vertex_remap.size());

		vertex_remap.insert({ index0, uint32_t(vertex_remap.size()) });
		vertex_remap.insert({ index1, uint32_t(vertex_remap.size()) });
		vertex_remap.insert({ index2, uint32_t(vertex_remap.size()) });

		// If this primitive causes us to go out of bounds, reset.
		if (vertex_remap.size() > MaxVertices)
		{
			max_num_primitives = i;
			break;
		}

		vertex_count = uint32_t(vertex_remap.size());
	}

	result.num_primitives = max_num_primitives;
	result.num_vertices = vertex_count;
	return result;
}

// Analyze bits required to encode a signed delta.
static uvec4 compute_required_bits_unsigned(u8vec4 delta)
{
	uvec4 result;
	for (unsigned i = 0; i < 4; i++)
	{
		uint32_t v = delta[i];
		result[i] = v == 0 ? 0 : (32 - leading_zeroes(v));
	}
	return result;
}

static uvec4 compute_required_bits_signed(u8vec4 delta)
{
	uvec4 result;
	for (unsigned i = 0; i < 4; i++)
	{
		uint32_t v = delta[i];

		if (v == 0)
		{
			result[i] = 0;
		}
		else
		{
			if (v >= 0x80u)
				v ^= 0xffu;
			result[i] = v == 0 ? 1 : (33 - leading_zeroes(v));
		}
	}
	return result;
}

static uint32_t extract_bit_plane(const uint8_t *bytes, unsigned bit_index)
{
	uint32_t u32 = 0;
	for (unsigned i = 0; i < 32; i++)
		u32 |= ((bytes[4 * i] >> bit_index) & 1u) << i;
	return u32;
}

static void find_linear_predictor(uint16_t *predictor,
                                  const u8vec4 (&stream_buffer)[MaxElements],
                                  unsigned num_elements)
{
	// Sign-extend since the deltas are considered to be signed ints.
	ivec4 unrolled_data[MaxElements];
	for (unsigned i = 0; i < num_elements; i++)
		unrolled_data[i] = ivec4(i8vec4(stream_buffer[i]));

	// Simple linear regression.
	// Pilfered from: https://www.codesansar.com/numerical-methods/linear-regression-method-using-c-programming.htm
	ivec4 x{0}, x2{0}, y{0}, xy{0};
	for (unsigned i = 0; i < num_elements; i++)
	{
		x += int(i);
		x2 += int(i * i);
		y += unrolled_data[i];
		xy += int(i) * unrolled_data[i];
	}

	int n = int(num_elements);
	ivec4 b_denom = (n * x2 - x * x);
	b_denom = select(b_denom, ivec4(1), equal(ivec4(0), b_denom));

	// Encode in u8.8 fixed point.
	ivec4 b = (ivec4(256) * (n * xy - x * y)) / b_denom;
	ivec4 a = ((ivec4(256) * y - b * x)) / n;

	for (unsigned i = 0; i < 4; i++)
		predictor[i] = uint16_t(a[i]);
	for (unsigned i = 0; i < 4; i++)
		predictor[4 + i] = uint16_t(b[i]);
}

static void encode_stream(std::vector<uint32_t> &out_payload_buffer,
                          MeshletStream &stream, u8vec4 (&stream_buffer)[MaxElements],
                          unsigned num_elements)
{
	stream.offset_from_base_u32 = uint32_t(out_payload_buffer.size());

	// Delta-encode
	u8vec4 current_value;
	if (num_elements > 1)
		current_value = u8vec4(2) * stream_buffer[0] - stream_buffer[1];
	else
		current_value = stream_buffer[0];
	u8vec4 bias_value = current_value;

	for (unsigned i = 0; i < num_elements; i++)
	{
		u8vec4 next_value = stream_buffer[i];
		stream_buffer[i] = next_value - current_value;
		current_value = next_value;
	}

	// Find optimal linear predictor.
	find_linear_predictor(stream.predictor, stream_buffer, num_elements);

	// u8.8 fixed point.
	auto base_predictor = u16vec4(stream.predictor[0], stream.predictor[1], stream.predictor[2], stream.predictor[3]);
	auto linear_predictor = u16vec4(stream.predictor[4], stream.predictor[5], stream.predictor[6], stream.predictor[7]);

	for (unsigned i = 0; i < num_elements; i++)
	{
		// Only predict in-bounds elements, since we want all out of bounds elements to be encoded to 0 delta
		// without having them affect the predictor.
		stream_buffer[i] -= u8vec4((base_predictor + linear_predictor * uint16_t(i)) >> uint16_t(8));
	}

	for (unsigned i = num_elements; i < MaxElements; i++)
		stream_buffer[i] = u8vec4(0);

	// Try to adjust the range such that it can fit in fewer bits.
	// We can use the constant term in the linear predictor to nudge values in place.
	i8vec4 lo(127);
	i8vec4 hi(-128);

	for (unsigned i = 0; i < num_elements; i++)
	{
		lo = min(lo, i8vec4(stream_buffer[i]));
		hi = max(hi, i8vec4(stream_buffer[i]));
	}

	uvec4 full_bits = compute_required_bits_unsigned(u8vec4(hi - lo));
	u8vec4 target_lo_value = u8vec4(-((uvec4(1) << full_bits) >> 1u));
	u8vec4 bias = target_lo_value - u8vec4(lo);

	for (unsigned i = 0; i < num_elements; i++)
		stream_buffer[i] += bias;

	for (unsigned i = 0; i < 4; i++)
		stream.predictor[i] -= uint16_t(bias[i]) << 8;

	// Based on the linear predictor, it's possible that the encoded value in stream_buffer[0] becomes non-zero again.
	// This is undesirable, since we can use the initial value to force a delta of 0 here, saving precious bits.
	bias_value += stream_buffer[0];
	stream_buffer[0] = u8vec4(0);

	// Simple linear predictor, base equal elements[0], gradient = 0.
	stream.predictor[8] = uint16_t((bias_value.y << 8) | bias_value.x);
	stream.predictor[9] = uint16_t((bias_value.w << 8) | bias_value.z);

	// Encode 32 elements at once.
	for (unsigned chunk_index = 0; chunk_index < MaxElements / 32; chunk_index++)
	{
		uvec4 required_bits = {};
		for (unsigned i = 0; i < 32; i++)
			required_bits = max(required_bits, compute_required_bits_signed(stream_buffer[chunk_index * 32 + i]));

		// Encode bit counts.
		stream.bitplane_meta[chunk_index] = uint16_t((required_bits.x << 0) | (required_bits.y << 4) |
		                                             (required_bits.z << 8) | (required_bits.w << 12));

		for (unsigned i = 0; i < required_bits.x; i++)
			out_payload_buffer.push_back(extract_bit_plane(&stream_buffer[chunk_index * 32][0], i));
		for (unsigned i = 0; i < required_bits.y; i++)
			out_payload_buffer.push_back(extract_bit_plane(&stream_buffer[chunk_index * 32][1], i));
		for (unsigned i = 0; i < required_bits.z; i++)
			out_payload_buffer.push_back(extract_bit_plane(&stream_buffer[chunk_index * 32][2], i));
		for (unsigned i = 0; i < required_bits.w; i++)
			out_payload_buffer.push_back(extract_bit_plane(&stream_buffer[chunk_index * 32][3], i));
	}
}

static void encode_mesh(std::vector<uint32_t> &out_payload_buffer, MeshMetadata &mesh,
                        const uint32_t *index_buffer, uint32_t primitive_count,
                        const uint32_t *attributes,
                        unsigned num_u32_streams)
{
	mesh = {};
	mesh.stream_count = num_u32_streams + 1;
	mesh.data_stream_offset_u32 = 0; // Can be adjusted in isolation later to pack multiple payload streams into one buffer.
	mesh.meshlets.reserve((primitive_count + MaxPrimitives - 1) / MaxPrimitives);
	uint32_t base_vertex_offset = 0;

	std::unordered_map<uint32_t, uint32_t> vbo_remap;

	for (uint32_t primitive_index = 0; primitive_index < primitive_count; )
	{
		uint32_t primitives_to_process = min(primitive_count - primitive_index, MaxPrimitives);
		auto analysis_result = analyze_primitive_count(vbo_remap, index_buffer + 3 * primitive_index, primitives_to_process);
		primitives_to_process = analysis_result.num_primitives;

		MeshletMetadata meshlet = {};
		u8vec4 stream_buffer[MaxElements];

		meshlet.base_vertex_offset = base_vertex_offset;
		meshlet.num_primitives_minus_1 = analysis_result.num_primitives - 1;
		meshlet.num_attributes_minus_1 = analysis_result.num_vertices - 1;
		meshlet.reserved = 0;

		// Encode index buffer.
		for (uint32_t i = 0; i < analysis_result.num_primitives; i++)
		{
			uint8_t i0 = vbo_remap[index_buffer[3 * (primitive_index + i) + 0]];
			uint8_t i1 = vbo_remap[index_buffer[3 * (primitive_index + i) + 1]];
			uint8_t i2 = vbo_remap[index_buffer[3 * (primitive_index + i) + 2]];
			//LOGI("Prim %u = { %u, %u, %u }\n", i, i0, i1, i2);
			stream_buffer[i] = u8vec4(i0, i1, i2, 0);
		}

		encode_stream(out_payload_buffer, meshlet.u32_streams[0], stream_buffer, analysis_result.num_primitives);

		// Handle spill region just in case.
		uint64_t vbo_remapping[MaxVertices + 3];
		unsigned vbo_index = 0;
		for (auto &v : vbo_remap)
		{
			assert(vbo_index < MaxVertices + 3);
			vbo_remapping[vbo_index++] = (uint64_t(v.second) << 32) | v.first;
		}
		std::sort(vbo_remapping, vbo_remapping + vbo_index);

		for (uint32_t stream_index = 0; stream_index < num_u32_streams; stream_index++)
		{
			for (uint32_t i = 0; i < analysis_result.num_vertices; i++)
			{
				auto vertex_index = uint32_t(vbo_remapping[i]);
				uint32_t payload = attributes[stream_index + num_u32_streams * vertex_index];
				memcpy(stream_buffer[i].data, &payload, sizeof(payload));
			}

			encode_stream(out_payload_buffer, meshlet.u32_streams[stream_index + 1], stream_buffer,
			              analysis_result.num_vertices);
		}

		mesh.meshlets.push_back(meshlet);

		primitive_index += primitives_to_process;
		base_vertex_offset += analysis_result.num_vertices;
	}

	mesh.data_stream_size_u32 = uint32_t(out_payload_buffer.size());
}

static void decode_mesh_setup_buffers(
		std::vector<uint32_t> &out_index_buffer, std::vector<uint32_t> &out_u32_stream, const MeshMetadata &mesh)
{
	assert(mesh.stream_count > 1);
	assert(mesh.stream_meta[0].type == StreamType::Primitive);
	assert(mesh.stream_meta[0].stream_index_component == 0);

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
                        const std::vector<uint32_t> &payload, const MeshMetadata &mesh)
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
		const std::vector<uint32_t> &payload, const MeshMetadata &mesh)
{
	decode_mesh_setup_buffers(out_index_buffer, out_u32_stream, mesh);
	const uint32_t u32_stride = mesh.stream_count - 1;

	Vulkan::BufferCreateInfo buf_info = {};
	buf_info.domain = Vulkan::BufferDomain::LinkedDeviceHost;
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	std::vector<MeshletMetadataGPU> meshlet_metas;
	meshlet_metas.reserve(mesh.meshlets.size());
	for (auto &meshlet : mesh.meshlets)
		meshlet_metas.push_back(meshlet);
	buf_info.size = mesh.meshlets.size() * sizeof(MeshletMetadataGPU);
	auto meshlet_meta_buffer = dev.create_buffer(buf_info, meshlet_metas.data());

	std::vector<MeshletStream> meshlet_streams;
	meshlet_streams.reserve(mesh.meshlets.size() * mesh.stream_count);
	for (auto &meshlet : mesh.meshlets)
		for (unsigned i = 0; i < mesh.stream_count; i++)
			meshlet_streams.push_back(meshlet.u32_streams[i]);
	buf_info.size = meshlet_streams.size() * sizeof(MeshletStream);
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

#if 1
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