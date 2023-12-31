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

template <int Components, typename T>
static void decode_bitfield_block_8(T *block, const PayloadB128 *&pdata, unsigned config)
{
	unsigned bit_offset = 0;

	for (int mask = 4; mask; mask >>= 1)
	{
		if (config & mask)
		{
			const uint32_t *words = &pdata->words[0];
			int bits = mask;

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

static void decode_attribute_buffer(std::vector<vec2> &out_uvs, const MeshView &mesh, uint32_t meshlet_index, StreamType type)
{
	auto &meshlet = mesh.headers[meshlet_index];
	auto &index_stream = mesh.streams[meshlet_index * mesh.format_header->stream_count + int(StreamType::Primitive)];
	auto &stream = mesh.streams[meshlet_index * mesh.format_header->stream_count + int(type)];
	const auto *pdata = mesh.payload + stream.offset_in_b128;

	for (uint32_t chunk = 0; chunk < meshlet.num_chunks; chunk++)
	{
		u16vec2 uvs[ElementsPerChunk]{};
		unsigned config = (stream.bit_plane_config >> (4 * chunk)) & 0xf;

		if (config == 8)
		{
			for (uint32_t i = 0; i < ElementsPerChunk; i++)
				memcpy(uvs[i].data, &pdata[i / 4].words[i % 4], 2 * sizeof(uint16_t));

			pdata += 8;
		}
		else
		{
			decode_bitfield_block_16<2>(uvs, pdata, config);
		}

		u16vec2 base;
		memcpy(base.data, &stream.u.base_value[chunk], sizeof(uint16_t) * 2);

		for (auto &p : uvs)
			p += base;

		uint32_t num_attributes_for_chunk = index_stream.u.offsets[chunk + 1].attr_offset -
		                                    index_stream.u.offsets[chunk].attr_offset;

		for (uint32_t i = 0; i < num_attributes_for_chunk; i++)
		{
			vec2 float_pos = vec2(i16vec2(uvs[i]));
			float_pos.x = ldexpf(float_pos.x, stream.aux);
			float_pos.y = ldexpf(float_pos.y, stream.aux);
			out_uvs.push_back(0.5f * float_pos + 0.5f);
		}
	}
}

static vec3 decode_oct8(i8vec2 payload)
{
	vec2 f = vec2(payload) * (1.0f / 127.0f);
	vec3 n = vec3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
	float t = max(-n.z, 0.0f);

	if (n.x > 0.0f)
		n.x -= t;
	else
		n.x += t;

	if (n.y > 0.0f)
		n.y -= t;
	else
		n.y += t;

	return normalize(n);
}

static void decode_attribute_buffer(std::vector<vec3> &out_normals, std::vector<vec4> &out_tangents,
                                    const MeshView &mesh, uint32_t meshlet_index, StreamType type)
{
	auto &meshlet = mesh.headers[meshlet_index];
	auto &index_stream = mesh.streams[meshlet_index * mesh.format_header->stream_count + int(StreamType::Primitive)];
	auto &stream = mesh.streams[meshlet_index * mesh.format_header->stream_count + int(type)];
	const auto *pdata = mesh.payload + stream.offset_in_b128;

	for (uint32_t chunk = 0; chunk < meshlet.num_chunks; chunk++)
	{
		u8vec4 nts[ElementsPerChunk]{};
		uint32_t t_signs = 0;

		unsigned config = (stream.bit_plane_config >> (4 * chunk)) & 0xf;

		if (config == 8)
		{
			memcpy(nts, pdata, sizeof(nts));
			pdata += 8;
		}
		else
		{
			decode_bitfield_block_8<4>(nts, pdata, config);
		}

		int aux = (stream.aux >> (2 * chunk)) & 3;

		if (aux == 1)
		{
			t_signs = 0;
		}
		else if (aux == 2)
		{
			t_signs = UINT32_MAX;
		}

		u8vec4 base;
		memcpy(base.data, &stream.u.base_value[chunk], sizeof(base.data));

		for (auto &p : nts)
			p += base;

		if (aux == 3)
		{
			for (unsigned i = 0; i < ElementsPerChunk; i++)
			{
				t_signs |= (nts[i].w & 1u) << i;
				nts[i].w &= ~1;
			}
		}

		uint32_t num_attributes_for_chunk = index_stream.u.offsets[chunk + 1].attr_offset -
		                                    index_stream.u.offsets[chunk].attr_offset;

		for (uint32_t i = 0; i < num_attributes_for_chunk; i++)
		{
			vec3 n = decode_oct8(i8vec2(nts[i].xy()));
			vec3 t = decode_oct8(i8vec2(nts[i].zw()));
			out_normals.push_back(n);
			out_tangents.emplace_back(t, (t_signs & (1u << i)) != 0 ? -1.0f : 1.0f);
		}
	}
}

static void decode_mesh(std::vector<uvec3> &out_index_buffer,
                        std::vector<vec3> &out_positions,
						std::vector<vec2> &out_uvs,
						std::vector<vec3> &out_normals,
						std::vector<vec4> &out_tangents,
                        const MeshView &mesh)
{
	for (uint32_t meshlet_index = 0; meshlet_index < mesh.format_header->meshlet_count; meshlet_index++)
	{
		decode_mesh_index_buffer(out_index_buffer, mesh, meshlet_index);
		decode_attribute_buffer(out_positions, mesh, meshlet_index, StreamType::Position);
		decode_attribute_buffer(out_uvs, mesh, meshlet_index, StreamType::UV);
		decode_attribute_buffer(out_normals, out_tangents, mesh, meshlet_index, StreamType::NormalTangentOct8);
	}
}

static vec4 decode_bgr10a2(uint32_t v)
{
	vec4 fvalue = vec4(ivec4((uvec4(v) >> uvec4(0, 10, 20, 30)) & 0x3ffu) - ivec4(512, 512, 512, 2)) *
	              vec4(1.0f / 511.0f, 1.0f / 511.0f, 1.0f / 511.0f, 1.0f);
	fvalue = clamp(fvalue, vec4(-1.0f), vec4(1.0f));
	return fvalue;
}

struct DecodedAttr
{
	uint32_t n;
	uint32_t t;
	vec2 uv;
};

static void decode_mesh_gpu_bench(Vulkan::Device &dev, const MeshView &mesh)
{
	Vulkan::BufferCreateInfo buf_info = {};
	buf_info.domain = Vulkan::BufferDomain::Host;
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buf_info.size = mesh.format_header->payload_size_b128 * sizeof(PayloadB128);
	auto payload_buffer = dev.create_buffer(buf_info, mesh.payload);

	buf_info.size = mesh.total_primitives * sizeof(u8vec3);
	buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buf_info.domain = Vulkan::BufferDomain::Device;
	auto readback_decoded_index_buffer = dev.create_buffer(buf_info);

	buf_info.size = mesh.total_vertices * sizeof(vec3);
	buf_info.domain = Vulkan::BufferDomain::Device;
	auto readback_decoded_pos_buffer = dev.create_buffer(buf_info);

	buf_info.size = mesh.total_vertices * sizeof(DecodedAttr);
	buf_info.domain = Vulkan::BufferDomain::Device;
	auto readback_decoded_attr_buffer = dev.create_buffer(buf_info);

	DecodeInfo info = {};
	info.ibo = readback_decoded_index_buffer.get();
	info.streams[0] = readback_decoded_pos_buffer.get();
	info.streams[1] = readback_decoded_attr_buffer.get();
	info.target_style = mesh.format_header->style;
	info.payload = payload_buffer.get();

	constexpr unsigned ITER_PER_CONTEXT = 1000;
	for (unsigned j = 0; j < 100; j++)
	{
		auto cmd = dev.request_command_buffer();
		auto start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		for (unsigned i = 0; i < ITER_PER_CONTEXT; i++)
			decode_mesh(*cmd, info, mesh);
		auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		dev.register_time_interval("GPU", std::move(start_ts), std::move(end_ts), "Decode100");
		dev.submit(cmd);
		dev.next_frame_context();
	}

	double time_per_context = 0.0;
	const auto logger =  [&](const std::string &tag, const Vulkan::TimestampIntervalReport &report) {
		if (tag == "Decode100")
			time_per_context = report.time_per_frame_context;
	};

	dev.timestamp_log(logger);
	double prim_count_per_context = double(mesh.total_primitives) * ITER_PER_CONTEXT;
	double prims_per_second = prim_count_per_context / time_per_context;

	double input_bw = double(mesh.format_header->payload_size_b128) * sizeof(PayloadB128) *
	                  ITER_PER_CONTEXT / time_per_context;
	double output_bw = double(mesh.total_primitives) * sizeof(u8vec3) +
	                   double(mesh.total_vertices) * (sizeof(vec3) + sizeof(DecodedAttr)) *
	                   ITER_PER_CONTEXT / time_per_context;

	LOGE("Primitives / s: %.3f G\n", prims_per_second * 1e-9);
	LOGE("Payload read BW: %.3f GB/s\n", input_bw * 1e-9);
	LOGE("Payload write BW: %.3f GB/s\n", output_bw * 1e-9);

	dev.wait_idle();
}

static void decode_mesh_gpu(
		Vulkan::Device &dev,
		std::vector<uvec3> &out_index_buffer, std::vector<vec3> &out_pos_buffer,
		std::vector<vec2> &out_uvs, std::vector<vec3> &out_normals, std::vector<vec4> &out_tangents,
		const MeshView &mesh)
{
	out_index_buffer.resize(mesh.total_primitives);
	out_pos_buffer.resize(mesh.total_vertices);

	std::vector<DecodedAttr> out_attr_buffer(mesh.total_vertices);

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

	buf_info.size = out_attr_buffer.size() * sizeof(DecodedAttr);
	buf_info.domain = Vulkan::BufferDomain::CachedHost;
	auto readback_decoded_attr_buffer = dev.create_buffer(buf_info);

	bool has_renderdoc = Vulkan::Device::init_renderdoc_capture();
	if (has_renderdoc)
		dev.begin_renderdoc_capture();

	auto cmd = dev.request_command_buffer();

	DecodeInfo info = {};
	info.ibo = readback_decoded_index_buffer.get();
	info.streams[0] = readback_decoded_pos_buffer.get();
	info.streams[1] = readback_decoded_attr_buffer.get();
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

	out_uvs.clear();
	out_normals.clear();
	out_tangents.clear();

	out_uvs.reserve(mesh.total_vertices);
	out_normals.reserve(mesh.total_vertices);
	out_tangents.reserve(mesh.total_vertices);

	auto *attrs = static_cast<const DecodedAttr *>(dev.map_host_buffer(*readback_decoded_attr_buffer, Vulkan::MEMORY_ACCESS_READ_BIT));
	for (size_t i = 0, n = mesh.total_vertices; i < n; i++)
	{
		auto &attr = attrs[i];
		out_uvs.push_back(attr.uv);
		out_normals.push_back(decode_bgr10a2(attr.n).xyz());
		out_tangents.push_back(decode_bgr10a2(attr.t));
	}
}

struct Attr
{
	vec2 uv;
	vec3 n;
	vec4 t;
};

static void build_reference_mesh(std::vector<uvec3> &indices, std::vector<vec3> &positions, std::vector<Attr> &attr)
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

		Attr a = {};
		a.uv.x = 1.0f * float(i);
		a.uv.y = a.uv.x * 1.5f;
		a.n = normalize(vec3(1.0f + float(i), 1.0f, -0.3f));
		a.t = vec4(a.n.y, -a.n.z, a.n.x, +1.0f);
		if (i & 1)
			a.t.w = -1.0f;
		attr.push_back(a);
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

template <typename T>
static auto max_component(T value) -> std::remove_reference_t<decltype(value.data[0])>
{
	std::remove_reference_t<decltype(value.data[0])> val = 0;
	for (auto v : value.data)
		val = std::max(val, v);
	return val;
}

template <typename T, typename ScalarT>
static bool validate_mesh_attribute(const std::vector<uvec3> &reference_indices,
                                    const std::vector<T> &reference_attr,
                                    const std::vector<uvec3> &decoded_indices,
                                    const std::vector<T> &decoded_attr, ScalarT tolerance)
{
	if (reference_indices.size() != decoded_indices.size())
	{
		LOGE("Mismatch in index buffer size.\n");
		return false;
	}

	for (size_t i = 0, n = decoded_indices.size(); i < n; i++)
	{
		uvec3 ref_i = reference_indices[i];
		uvec3 decode_i = decoded_indices[i];

		for (int c = 0; c < 3; c++)
		{
			auto ref_attr = reference_attr[ref_i[c]];
			auto decode_attr = decoded_attr[decode_i[c]];
			auto d = abs(ref_attr - decode_attr);
			auto max_d = max_component(d);
			if (max_d > tolerance)
			{
				LOGE("Mismatch in primitive %zu, c = %d.\n", i, c);
				return false;
			}
		}
	}

	return true;
}

int main(int argc, char **argv)
{
	Global::init(Global::MANAGER_FEATURE_FILESYSTEM_BIT);
	Filesystem::setup_default_filesystem(GRANITE_FILESYSTEM(), ASSET_DIRECTORY);

	SceneFormats::Mesh mesh;
	std::vector<uvec3> reference_indices;
	std::vector<vec3> reference_positions;
	std::vector<Attr> reference_attributes;

	if (argc == 2)
	{
		GLTF::Parser parser(argv[1]);
		if (parser.get_meshes().empty())
			return EXIT_FAILURE;
		mesh = parser.get_meshes().front();
	}
	else
	{
		build_reference_mesh(reference_indices, reference_positions, reference_attributes);

		mesh.index_type = VK_INDEX_TYPE_UINT32;
		mesh.count = 3 * reference_indices.size();
		mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		mesh.indices.resize(mesh.count * sizeof(uint32_t));
		memcpy(mesh.indices.data(), reference_indices.data(), mesh.count * sizeof(uint32_t));

		mesh.attribute_layout[int(MeshAttribute::Position)].format = VK_FORMAT_R32G32B32_SFLOAT;
		mesh.position_stride = sizeof(vec3);
		mesh.positions.resize(reference_positions.size() * sizeof(vec3));
		memcpy(mesh.positions.data(), reference_positions.data(), reference_positions.size() * sizeof(vec3));

		mesh.attribute_layout[int(MeshAttribute::UV)].format = VK_FORMAT_R32G32_SFLOAT;
		mesh.attribute_layout[int(MeshAttribute::UV)].offset = offsetof(Attr, uv);
		mesh.attribute_layout[int(MeshAttribute::Normal)].format = VK_FORMAT_R32G32B32_SFLOAT;
		mesh.attribute_layout[int(MeshAttribute::Normal)].offset = offsetof(Attr, n);
		mesh.attribute_layout[int(MeshAttribute::Tangent)].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		mesh.attribute_layout[int(MeshAttribute::Tangent)].offset = offsetof(Attr, t);
		mesh.attribute_stride = sizeof(Attr);
		mesh.attributes.resize(mesh.attribute_stride * reference_attributes.size());
		memcpy(mesh.attributes.data(), reference_attributes.data(), mesh.attributes.size());
	}

	if (!Meshlet::export_mesh_to_meshlet("export.msh2", std::move(mesh), MeshStyle::Textured))
		return EXIT_FAILURE;

	auto file = GRANITE_FILESYSTEM()->open("export.msh2", FileMode::ReadOnly);
	if (!file)
		return EXIT_FAILURE;

	auto mapped = file->map();
	if (!mapped)
		return EXIT_FAILURE;

	std::vector<uvec3> decoded_index_buffer;
	std::vector<vec3> decoded_positions;
	std::vector<vec2> decoded_uvs;
	std::vector<vec3> decoded_normals;
	std::vector<vec4> decoded_tangents;
	auto view = create_mesh_view(*mapped);
	decode_mesh(decoded_index_buffer, decoded_positions, decoded_uvs, decoded_normals, decoded_tangents, view);

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
	std::vector<vec2> gpu_uvs;
	std::vector<vec3> gpu_normals;
	std::vector<vec4> gpu_tangents;
	decode_mesh_gpu(dev, gpu_index_buffer, gpu_positions, gpu_uvs, gpu_normals, gpu_tangents, view);

	if (!validate_mesh(decoded_index_buffer, decoded_positions,
	                   gpu_index_buffer, gpu_positions, false))
		return EXIT_FAILURE;

	if (!reference_indices.empty())
	{
		if (!validate_mesh(reference_indices, reference_positions, decoded_index_buffer, decoded_positions, true))
			return EXIT_FAILURE;
	}

	std::vector<vec2> reference_uvs;
	std::vector<vec3> reference_normals;
	std::vector<vec4> reference_tangents;
	reference_uvs.reserve(reference_attributes.size());
	reference_normals.reserve(reference_attributes.size());
	reference_tangents.reserve(reference_attributes.size());
	for (auto &a : reference_attributes)
	{
		reference_uvs.push_back(a.uv);
		reference_normals.push_back(a.n);
		reference_tangents.push_back(a.t);
	}

	if (!reference_indices.empty())
	{
		if (!validate_mesh_attribute(reference_indices, reference_uvs, decoded_index_buffer, decoded_uvs, 0.0f))
			return EXIT_FAILURE;
		if (!validate_mesh_attribute(reference_indices, reference_normals, decoded_index_buffer, decoded_normals,
		                             0.02f))
			return EXIT_FAILURE;
		if (!validate_mesh_attribute(reference_indices, reference_tangents, decoded_index_buffer, decoded_tangents,
		                             0.02f))
			return EXIT_FAILURE;
	}

	decode_mesh_gpu_bench(dev, view);

	return 0;
}