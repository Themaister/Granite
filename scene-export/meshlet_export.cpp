/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "meshlet_export.hpp"
#include "meshoptimizer.h"
#include "enum_cast.hpp"
#include "math.hpp"
#include "filesystem.hpp"
#include "meshlet.hpp"
#include <type_traits>
#include <limits>

namespace Granite
{
namespace Meshlet
{
using namespace Vulkan::Meshlet;

struct Metadata
{
	Stream streams[MaxStreams];
};

struct CombinedMesh
{
	uint32_t stream_count;
	MeshStyle mesh_style;

	std::vector<Metadata> meshlets;
};

struct Encoded
{
	std::vector<PayloadWord> payload;
	std::vector<Bound> bounds;
	CombinedMesh mesh;
};

struct Meshlet
{
	uint32_t global_indices_offset;
	uint32_t primitive_count;
	uint32_t vertex_count;

	const unsigned char *local_indices;
	const uint32_t *attribute_remap;
};

static i16vec3 encode_vec3_to_snorm_exp(vec3 v, int scale_log2)
{
	v.x = ldexpf(v.x, scale_log2);
	v.y = ldexpf(v.y, scale_log2);
	v.z = ldexpf(v.z, scale_log2);
	v = clamp(round(v), vec3(-0x8000), vec3(0x7fff));
	return i16vec3(v);
}

static i16vec2 encode_vec2_to_snorm_exp(vec2 v, int scale_log2)
{
	v.x = ldexpf(v.x, scale_log2);
	v.y = ldexpf(v.y, scale_log2);
	v = clamp(round(v), vec2(-0x8000), vec2(0x7fff));
	return i16vec2(v);
}

static int compute_log2_scale(float max_value)
{
	// Maximum component should have range of [1, 2) since we use floor of log2, so scale with 2^14 instead of 15.
	int max_scale_log2 = int(muglm::floor(muglm::log2(max_value)));
	int scale_log2 = 14 - max_scale_log2;
	return scale_log2;
}

template <typename T>
static void adjust_quant(std::vector<T> &values, int &exp)
{
	uint32_t active_bits = 0;
	for (auto &value : values)
		for (auto &c : value.data)
			active_bits |= c;

	if (active_bits == 0)
		return;

	int extra_shift = Util::trailing_zeroes(active_bits);
	for (auto &value : values)
		for (auto &c : value.data)
			c >>= extra_shift;

	exp += extra_shift;
}

static std::vector<i16vec3> mesh_extract_position_snorm_exp(const SceneFormats::Mesh &mesh, int &exp)
{
	std::vector<i16vec3> encoded_positions;
	std::vector<vec3> positions;

	size_t num_positions = mesh.positions.size() / mesh.position_stride;
	positions.resize(num_positions);
	auto &layout = mesh.attribute_layout[Util::ecast(MeshAttribute::Position)];
	auto fmt = layout.format;

	if (fmt == VK_FORMAT_R32G32B32A32_SFLOAT || fmt == VK_FORMAT_R32G32B32_SFLOAT)
	{
		for (size_t i = 0; i < num_positions; i++)
		{
			memcpy(positions[i].data,
				   mesh.positions.data() + i * mesh.position_stride + layout.offset,
			       sizeof(float) * 3);
		}
	}
	else if (fmt == VK_FORMAT_UNDEFINED)
		return {};
	else
	{
		LOGE("Unexpected format %u.\n", fmt);
		return {};
	}

	vec3 max_extent = vec3(0.0f);
	for (auto &p : positions)
		max_extent = max(max_extent, abs(p));

	float max_value = max(max(max_extent.x, max_extent.y), max_extent.z);
	int log2_scale = compute_log2_scale(max_value);

	log2_scale = std::min(log2_scale, 12);

	encoded_positions.reserve(positions.size());
	for (auto &pos : positions)
		encoded_positions.push_back(encode_vec3_to_snorm_exp(pos, log2_scale));

	exp = -log2_scale;
	adjust_quant(encoded_positions, exp);

	return encoded_positions;
}

struct NormalTangent
{
	i8vec2 n;
	i8vec2 t;
	bool t_sign;
};

static std::vector<NormalTangent> mesh_extract_normal_tangent_oct8(const SceneFormats::Mesh &mesh)
{
	std::vector<NormalTangent> encoded_attributes;
	std::vector<vec4> normals;
	std::vector<vec4> tangents;

	auto &normal = mesh.attribute_layout[Util::ecast(MeshAttribute::Normal)];
	auto &tangent = mesh.attribute_layout[Util::ecast(MeshAttribute::Tangent)];

	size_t num_attrs = mesh.attributes.size() / mesh.attribute_stride;
	normals.resize(num_attrs);
	tangents.resize(num_attrs);

	if (normal.format == VK_FORMAT_R32G32B32_SFLOAT || normal.format == VK_FORMAT_R32G32B32A32_SFLOAT)
	{
		for (size_t i = 0; i < num_attrs; i++)
		{
			memcpy(normals[i].data,
			       mesh.attributes.data() + i * mesh.attribute_stride + normal.offset,
			       sizeof(float) * 3);
		}
	}
	else if (normal.format == VK_FORMAT_UNDEFINED)
	{
		for (auto &n : normals)
			n = {};
	}
	else
	{
		LOGE("Unexpected format %u.\n", normal.format);
		return {};
	}

	if (tangent.format == VK_FORMAT_R32G32B32_SFLOAT)
	{
		for (size_t i = 0; i < num_attrs; i++)
		{
			memcpy(tangents[i].data,
			       mesh.attributes.data() + i * mesh.attribute_stride + tangent.offset,
			       sizeof(float) * 3);
			tangents[i].w = 0.0f;
		}
	}
	else if (tangent.format == VK_FORMAT_R32G32B32A32_SFLOAT)
	{
		for (size_t i = 0; i < num_attrs; i++)
		{
			memcpy(tangents[i].data,
			       mesh.attributes.data() + i * mesh.attribute_stride + tangent.offset,
			       sizeof(float) * 4);
		}
	}
	else if (tangent.format == VK_FORMAT_UNDEFINED)
	{
		for (auto &t : tangents)
			t = {};
	}
	else
	{
		LOGE("Unexpected format %u.\n", tangent.format);
		return {};
	}

	encoded_attributes.reserve(normals.size());

	std::vector<i8vec4> n(normals.size());
	std::vector<i8vec4> t(normals.size());
	meshopt_encodeFilterOct(n.data(), n.size(), sizeof(i8vec4), 8, normals[0].data);
	meshopt_encodeFilterOct(t.data(), t.size(), sizeof(i8vec4), 8, tangents[0].data);

	for (size_t i = 0, size = normals.size(); i < size; i++)
		encoded_attributes.push_back({ n[i].xy(), t[i].xy(), tangents[i].w < 0.0f });

	return encoded_attributes;
}

static std::vector<i16vec2> mesh_extract_uv_snorm_scale(const SceneFormats::Mesh &mesh, int &exp)
{
	std::vector<i16vec2> encoded_uvs;
	std::vector<vec2> uvs;

	size_t num_uvs = mesh.attributes.size() / mesh.attribute_stride;
	uvs.resize(num_uvs);
	auto &layout = mesh.attribute_layout[int(MeshAttribute::UV)];
	auto fmt = layout.format;

	if (fmt == VK_FORMAT_R32G32_SFLOAT)
	{
		for (size_t i = 0; i < num_uvs; i++)
			memcpy(uvs[i].data, mesh.attributes.data() + i * mesh.attribute_stride + layout.offset, sizeof(float) * 2);
	}
	else if (fmt == VK_FORMAT_R16G16_UNORM)
	{
		for (size_t i = 0; i < num_uvs; i++)
		{
			u16vec2 u16;
			memcpy(u16.data, mesh.attributes.data() + i * mesh.attribute_stride + layout.offset, sizeof(uint16_t) * 2);
			uvs[i] = vec2(u16) * float(1.0f / 0xffff);
		}
	}
	else if (fmt == VK_FORMAT_UNDEFINED)
	{
		for (auto &uv : uvs)
			uv = {};
	}
	else
	{
		LOGE("Unexpected format %u.\n", fmt);
		return {};
	}

	vec2 max_extent = vec2(0.0f);
	for (auto &uv : uvs)
	{
		// UVs tend to be in [0, 1] range. Readjust to use more of the available range.
		uv = 2.0f * uv - 1.0f;
		max_extent = max(max_extent, abs(uv));
	}

	float max_value = max(max_extent.x, max_extent.y);
	int log2_scale = compute_log2_scale(max_value);

	encoded_uvs.reserve(uvs.size());
	for (auto &uv : uvs)
		encoded_uvs.push_back(encode_vec2_to_snorm_exp(uv, log2_scale));

	exp = -log2_scale;
	adjust_quant(encoded_uvs, exp);

	return encoded_uvs;
}

// Analyze bits required to encode a delta.
static uint32_t compute_required_bits_unsigned(uint32_t delta)
{
	return delta == 0 ? 0 : (32 - Util::leading_zeroes(delta));
}

static vec3 decode_snorm_exp(i16vec3 p, int exp)
{
    vec3 result;
    result.x = ldexpf(float(p.x), exp);
    result.y = ldexpf(float(p.y), exp);
    result.z = ldexpf(float(p.z), exp);
    return result;
}

template <typename T>
static void write_bits(PayloadWord *words, const T *values, unsigned component_count,
                       unsigned element_index, unsigned bit_count)
{
	unsigned bit_offset = element_index * component_count * bit_count;
	for (unsigned c = 0; c < component_count; c++)
	{
		auto value = values[c];
		for (unsigned i = 0; i < bit_count; i++, bit_offset++)
			words[bit_offset / 32] |= ((value >> i) & 1) << (bit_offset & 31);
	}
}

static void encode_index_stream(std::vector<PayloadWord> &out_payload_buffer,
                                const u8vec3 *stream_buffer, unsigned count)
{
	PayloadWord p[15] = {};

	for (unsigned i = 0; i < count; i++)
	{
		u8vec3 indices = stream_buffer[i];
		assert(all(lessThan(indices, u8vec3(MaxElements))));
		write_bits(p, indices.data, 3, i, 5);
	}

	out_payload_buffer.insert(out_payload_buffer.end(), p, p + (15 * count + 31) / 32);
}

template <int Components, typename T>
static void encode_bitplane_16_inner(std::vector<PayloadWord> &out_payload_buffer,
                                     const T *values, unsigned encoded_bits, unsigned count)
{
	PayloadWord p[16 * Components] = {};
	for (uint32_t i = 0; i < count; i++)
		write_bits(p, values[i].data, Components, i, encoded_bits);

	out_payload_buffer.insert(out_payload_buffer.end(), p, p + (encoded_bits * Components * count + 31) / 32);
}

static void encode_bitplane(std::vector<PayloadWord> &out_payload_buffer,
                            const u8vec4 *values, unsigned encoded_bits, unsigned count)
{
	PayloadWord p[8 * 4] = {};
	for (uint32_t i = 0; i < count; i++)
		write_bits(p, values[i].data, 4, i, encoded_bits);

	out_payload_buffer.insert(out_payload_buffer.end(), p, p + (encoded_bits * 4 * count + 31) / 32);
}

static void encode_bitplane(std::vector<PayloadWord> &out_payload_buffer,
                            const u16vec3 *values, unsigned encoded_bits, unsigned count)
{
	encode_bitplane_16_inner<3>(out_payload_buffer, values, encoded_bits, count);
}

static void encode_bitplane(std::vector<PayloadWord> &out_payload_buffer,
                            const u16vec2 *values, unsigned encoded_bits, unsigned count)
{
	encode_bitplane_16_inner<2>(out_payload_buffer, values, encoded_bits, count);
}

template <typename T> struct to_signed_vector {};
template <typename T> struct to_components {};

template <> struct to_signed_vector<u16vec3> { using type = i16vec3; };
template <> struct to_signed_vector<u16vec2> { using type = i16vec2; };
template <> struct to_components<u16vec3> { enum { components = 3 }; };
template <> struct to_components<u16vec2> { enum { components = 2 }; };
template <> struct to_signed_vector<u8vec4> { using type = i8vec4; };
template <> struct to_components<u8vec4> { enum { components = 4 }; };

template <typename T>
static auto max_component(T value) -> std::remove_reference_t<decltype(value.data[0])>
{
	std::remove_reference_t<decltype(value.data[0])> val = 0;
	for (auto v : value.data)
		val = std::max(val, v);
	return val;
}

template <typename T>
static void encode_attribute_stream(std::vector<PayloadWord> &out_payload_buffer,
                                    Stream &stream,
                                    const T *raw_attributes,
                                    const uint32_t *vbo_remap,
                                    uint32_t num_attributes)
{
	using SignedT = typename to_signed_vector<T>::type;
	using UnsignedScalar = std::remove_reference_t<decltype(T()[0])>;
	using SignedScalar = std::remove_reference_t<decltype(SignedT()[0])>;
	static_assert(sizeof(T) == 4 || sizeof(T) == 6, "Encoded type must be 32 or 48 bits.");

	T attributes[MaxElements];
	for (uint32_t i = 0; i < num_attributes; i++)
		attributes[i] = raw_attributes[vbo_remap ? vbo_remap[i] : i];
	for (uint32_t i = num_attributes; i < MaxElements; i++)
		attributes[i] = attributes[0];

	T ulo{std::numeric_limits<UnsignedScalar>::max()};
	T uhi{std::numeric_limits<UnsignedScalar>::min()};
	SignedT slo{std::numeric_limits<SignedScalar>::max()};
	SignedT shi{std::numeric_limits<SignedScalar>::min()};

	for (auto &p : attributes)
	{
		ulo = min(ulo, p);
		uhi = max(uhi, p);
		slo = min(slo, SignedT(p));
		shi = max(shi, SignedT(p));
	}

	T diff_unsigned = uhi - ulo;
	T diff_signed = T(shi) - T(slo);

	unsigned diff_max_unsigned = max_component(diff_unsigned);
	unsigned diff_max_signed = max_component(diff_signed);
	if (diff_max_signed < diff_max_unsigned)
	{
		ulo = T(slo);
		diff_max_unsigned = diff_max_signed;
	}

	constexpr unsigned bits_per_component = sizeof(UnsignedScalar) * 8;

	unsigned bits = compute_required_bits_unsigned(diff_max_unsigned);

	if (bits_per_component == 16 && to_components<T>::components == 3)
	{
		// Decode math breaks for 13, 14 and 15 bits. Force 16-bit mode.
		// Encoder can choose to quantize a bit harder, so we can hit 12-bit mode.
		if (bits < 16 && bits > 12)
			bits = 16;
	}

	write_bits(stream.u.base_value, ulo.data, to_components<T>::components, 0, bits_per_component);
	stream.bits = bits;

	for (auto &p : attributes)
		p -= ulo;

	encode_bitplane(out_payload_buffer, attributes, bits, num_attributes);
}

static void encode_mesh(Encoded &encoded,
                        const Meshlet *meshlets, size_t num_meshlets,
						const void * const *pp_data,
						const int *p_aux,
                        unsigned num_streams)
{
	encoded = {};
	auto &mesh = encoded.mesh;
	assert(num_streams > 0);
	mesh.stream_count = num_streams;

	mesh.meshlets.reserve(num_meshlets);
	uint32_t base_vertex_offset = 0;
	uint32_t stream_payload_count[MaxStreams] = {};

	for (uint32_t meshlet_index = 0; meshlet_index < num_meshlets; meshlet_index++)
	{
		auto &meshlet = meshlets[meshlet_index];
		Metadata out_meshlet = {};

		{
			auto &index_stream = out_meshlet.streams[int(StreamType::Primitive)];
			index_stream.offset_in_words = uint32_t(encoded.payload.size());

			u8vec3 index_stream_buffer[MaxElements];
			for (uint32_t i = 0; i < meshlet.primitive_count; i++)
				memcpy(index_stream_buffer[i].data, meshlet.local_indices + 3 * i, 3);
			for (uint32_t i = meshlet.primitive_count; i < MaxElements; i++)
				index_stream_buffer[i] = u8vec3(0);

			auto &counts = index_stream.u.counts;
			counts.prim_count = meshlet.primitive_count;
			counts.vert_count = meshlet.vertex_count;

			auto start_count = encoded.payload.size();
			encode_index_stream(encoded.payload, index_stream_buffer, meshlet.primitive_count);
			auto end_count = encoded.payload.size();

			stream_payload_count[int(StreamType::Primitive)] += end_count - start_count;
			base_vertex_offset += meshlet.vertex_count;
		}

		for (uint32_t stream_index = 1; stream_index < num_streams; stream_index++)
		{
			auto &stream = out_meshlet.streams[stream_index];
			stream.offset_in_words = uint32_t(encoded.payload.size());

			uint32_t start_count = encoded.payload.size();
			switch (StreamType(stream_index))
			{
			case StreamType::Position:
				encode_attribute_stream(encoded.payload, stream,
				                        static_cast<const u16vec3 *>(pp_data[stream_index]),
				                        meshlet.attribute_remap, meshlet.vertex_count);
				stream.bits |= uint32_t(p_aux[stream_index] << 16);
				break;

			case StreamType::UV:
				encode_attribute_stream(encoded.payload, stream,
				                        static_cast<const u16vec2 *>(pp_data[stream_index]),
				                        meshlet.attribute_remap, meshlet.vertex_count);
				stream.bits |= uint32_t(p_aux[stream_index] << 16);
				break;

			case StreamType::NormalTangentOct8:
			{
				u8vec4 nts[MaxElements]{};
				uint32_t sign_mask = 0;
				auto *nt = static_cast<const NormalTangent *>(pp_data[stream_index]);
				for (unsigned i = 0; i < meshlet.vertex_count; i++)
				{
					const auto &mapped_nt = nt[meshlet.attribute_remap[i]];
					sign_mask |= uint32_t(mapped_nt.t_sign) << i;
					nts[i] = u8vec4(u8vec2(mapped_nt.n), u8vec2(mapped_nt.t));
				}

				if (meshlet.vertex_count < MaxElements && sign_mask == (1u << meshlet.vertex_count) - 1)
					sign_mask = UINT32_MAX;

				encode_attribute_stream(encoded.payload, stream, nts, nullptr, meshlet.vertex_count);

				if (sign_mask == 0)
				{
					stream.bits |= 1 << 16;
				}
				else if (sign_mask == UINT32_MAX)
				{
					stream.bits |= 2 << 16;
				}
				else
				{
					stream.bits |= 3 << 16;
					for (unsigned i = 0; i < meshlet.vertex_count; i++)
					{
						nts[i].w &= ~1;
						nts[i].w |= (sign_mask >> i) & 1u;
					}
				}

				break;
			}

			default:
				break;
			}

			uint32_t end_count = encoded.payload.size();
			stream_payload_count[stream_index] += end_count - start_count;
		}

		mesh.meshlets.push_back(out_meshlet);
	}

	for (unsigned i = 0; i < MaxStreams; i++)
		if (stream_payload_count[i])
			LOGI("Stream %u: %zu bytes.\n", i, stream_payload_count[i] * sizeof(PayloadWord));
	LOGI("Total encoded vertices: %u\n", base_vertex_offset);
}

static bool export_encoded_mesh(const std::string &path, const Encoded &encoded)
{
	size_t required_size = 0;

	FormatHeader header = {};

	header.style = encoded.mesh.mesh_style;
	header.stream_count = encoded.mesh.stream_count;
	header.meshlet_count = uint32_t(encoded.mesh.meshlets.size());
	header.payload_size_words = uint32_t(encoded.payload.size());

	required_size += sizeof(magic);
	required_size += sizeof(FormatHeader);

	// Bounds.
	required_size += encoded.bounds.size() * sizeof(Bound);

	// Stream metadata.
	required_size += encoded.mesh.stream_count * encoded.mesh.meshlets.size() * sizeof(Stream);

	// Payload.
	// Need a padding word to speed up decoder.
	required_size += (encoded.payload.size() + 1) * sizeof(PayloadWord);

	auto file = GRANITE_FILESYSTEM()->open(path, FileMode::WriteOnly);
	if (!file)
		return false;

	auto mapping = file->map_write(required_size);
	if (!mapping)
		return false;

	auto *ptr = mapping->mutable_data<unsigned char>();

	memcpy(ptr, magic, sizeof(magic));
	ptr += sizeof(magic);
	memcpy(ptr, &header, sizeof(header));
	ptr += sizeof(header);

	memcpy(ptr, encoded.bounds.data(), encoded.bounds.size() * sizeof(Bound));
	ptr += encoded.bounds.size() * sizeof(Bound);

	for (uint32_t i = 0; i < header.meshlet_count; i++)
	{
		for (uint32_t j = 0; j < header.stream_count; j++)
		{
			memcpy(ptr, &encoded.mesh.meshlets[i].streams[j], sizeof(Stream));
			ptr += sizeof(Stream);
		}
	}

	memcpy(ptr, encoded.payload.data(), encoded.payload.size() * sizeof(PayloadWord));
	ptr += encoded.payload.size() * sizeof(PayloadWord);
	memset(ptr, 0, sizeof(PayloadWord));
	return true;
}

static float compute_sq_dist(const Bound &a, const Bound &b)
{
	float dx = a.center[0] - b.center[0];
	float dy = a.center[1] - b.center[1];
	float dz = a.center[2] - b.center[2];
	return dx * dx + dy * dy + dz * dz;
}

static size_t find_closest_sphere(const Bound *bounds, size_t num_bounds, const Bound &reference_bound)
{
	size_t best_index = 0;
	float best_sq_dist = compute_sq_dist(bounds[0], reference_bound);
	for (size_t i = 1; i < num_bounds; i++)
	{
		float sq_dist = compute_sq_dist(bounds[i], reference_bound);
		if (sq_dist < best_sq_dist)
		{
			best_index = i;
			best_sq_dist = sq_dist;
		}
	}

	return best_index;
}

// FIXME: O(n^2). Revisit if this becomes a real problem.
static void sort_bounds(Bound *bound, size_t num_bounds,
                        Meshlet *meshlets, Metadata *metadata)
{
	for (size_t offset = 1; offset < num_bounds; offset++)
	{
		size_t cluster_offset = offset & ~(ChunkFactor - 1);

		// Starts a new cluster.
		if (cluster_offset == offset)
			continue;

		size_t index = find_closest_sphere(
				bound + offset, num_bounds - offset,
				bound[cluster_offset]);

		index += offset;

		if (index != offset)
		{
			std::swap(bound[offset], bound[index]);
			std::swap(meshlets[offset], meshlets[index]);
			std::swap(metadata[offset], metadata[index]);
		}
	}
}

static void encode_bounds(std::vector<Bound> &bounds,
                          const Meshlet *meshlets, size_t num_meshlets,
                          const uvec3 *out_index_buffer, const vec3 *position_buffer,
                          unsigned position_count,
                          unsigned chunk_factor)
{
	size_t num_new_bounds = (num_meshlets + chunk_factor - 1) / chunk_factor;
	bounds.reserve(bounds.size() + num_new_bounds);
	assert(chunk_factor <= ChunkFactor);

	float total_radius = 0.0f;
	float total_cutoff = 0.0f;

	for (size_t i = 0; i < num_meshlets; i += chunk_factor)
	{
		size_t num_chunks = std::min<size_t>(num_meshlets - i, chunk_factor);
		uvec3 tmp_indices[MaxElements * ChunkFactor];
		uint32_t total_count = 0;

		for (size_t chunk = 0; chunk < num_chunks; chunk++)
		{
			auto &meshlet = meshlets[i + chunk];
			memcpy(tmp_indices[total_count].data,
			       out_index_buffer[meshlet.global_indices_offset].data,
			       meshlet.primitive_count * sizeof(tmp_indices[0]));
			total_count += meshlet.primitive_count;
		}

		auto bound = meshopt_computeClusterBounds(
				tmp_indices[0].data, total_count * 3,
				position_buffer[0].data, position_count, sizeof(vec3));

		Bound encoded_bound = {};
		memcpy(encoded_bound.center, bound.center, sizeof(bound.center));
		encoded_bound.radius = bound.radius;
		memcpy(encoded_bound.cone_axis_cutoff, bound.cone_axis, sizeof(bound.cone_axis));
		encoded_bound.cone_axis_cutoff[3] = bound.cone_cutoff;
		bounds.push_back(encoded_bound);

		total_radius += bound.radius;
		total_cutoff += bound.cone_cutoff;
	}

	total_radius /= float(num_new_bounds);
	total_cutoff /= float(num_new_bounds);
	LOGI("Average radius %.3f (%zu bounds)\n", total_radius, num_new_bounds);
	LOGI("Average cutoff %.3f (%zu bounds)\n", total_cutoff, num_new_bounds);
}

bool export_mesh_to_meshlet(const std::string &path, SceneFormats::Mesh mesh, MeshStyle style)
{
	mesh_deduplicate_vertices(mesh);
	if (!mesh_optimize_index_buffer(mesh, {}))
		return false;

	std::vector<i16vec3> positions;
	std::vector<i16vec2> uv;
	std::vector<NormalTangent> normal_tangent;

	unsigned num_attribute_streams = 0;
	int aux[MaxStreams] = {};
	const void *p_data[MaxStreams] = {};

	switch (style)
	{
	case MeshStyle::Skinned:
		LOGE("Unimplemented.\n");
		return false;
	case MeshStyle::Textured:
		uv = mesh_extract_uv_snorm_scale(mesh, aux[int(StreamType::UV)]);
		num_attribute_streams += 2;
		if (uv.empty())
		{
			LOGE("No UVs.\n");
			return false;
		}
		normal_tangent = mesh_extract_normal_tangent_oct8(mesh);
		if (normal_tangent.empty())
		{
			LOGE("No tangent or normal.\n");
			return false;
		}
		p_data[int(StreamType::UV)] = uv.data();
		p_data[int(StreamType::NormalTangentOct8)] = normal_tangent.data();
		// Fallthrough
	case MeshStyle::Wireframe:
		positions = mesh_extract_position_snorm_exp(mesh, aux[int(StreamType::Position)]);
		if (positions.empty())
		{
			LOGE("No positions.\n");
			return false;
		}
		p_data[int(StreamType::Position)] = positions.data();
		num_attribute_streams += 1;
		break;

	default:
		LOGE("Unknown mesh style.\n");
		return false;
	}

	// Use quantized position to guide the clustering.
	std::vector<vec3> position_buffer;
	position_buffer.reserve(positions.size());
	for (auto &p : positions)
		position_buffer.push_back(decode_snorm_exp(p, aux[int(StreamType::Position)]));

	constexpr unsigned max_vertices = MaxElements;
	constexpr unsigned max_primitives = MaxElements;
	size_t num_meshlets = meshopt_buildMeshletsBound(mesh.count, max_vertices, max_primitives);

	std::vector<unsigned> out_vertex_redirection_buffer(num_meshlets * max_vertices);
	std::vector<unsigned char> local_index_buffer(num_meshlets * max_primitives * 3);
	std::vector<meshopt_Meshlet> meshlets(num_meshlets);

	num_meshlets = meshopt_buildMeshlets(meshlets.data(),
	                                     out_vertex_redirection_buffer.data(), local_index_buffer.data(),
	                                     reinterpret_cast<const uint32_t *>(mesh.indices.data()), mesh.count,
	                                     position_buffer[0].data, positions.size(), sizeof(vec3),
	                                     max_vertices, max_primitives, 0.5f);

	meshlets.resize(num_meshlets);

	std::vector<Meshlet> out_meshlets;
	std::vector<uvec3> out_index_buffer;

	out_meshlets.reserve(num_meshlets);

	for (auto &meshlet : meshlets)
	{
		Meshlet m = {};

		auto *local_indices = local_index_buffer.data() + meshlet.triangle_offset;
		m.local_indices = local_indices;
		m.attribute_remap = out_vertex_redirection_buffer.data() + meshlet.vertex_offset;
		m.primitive_count = meshlet.triangle_count;
		m.vertex_count = meshlet.vertex_count;
		m.global_indices_offset = uint32_t(out_index_buffer.size());

		for (unsigned i = 0; i < meshlet.triangle_count; i++)
		{
			out_index_buffer.emplace_back(
					out_vertex_redirection_buffer[local_indices[3 * i + 0] + meshlet.vertex_offset],
					out_vertex_redirection_buffer[local_indices[3 * i + 1] + meshlet.vertex_offset],
					out_vertex_redirection_buffer[local_indices[3 * i + 2] + meshlet.vertex_offset]);
		}

		out_meshlets.push_back(m);
	}

	Encoded encoded;
	encode_mesh(encoded, out_meshlets.data(), out_meshlets.size(),
	            p_data, aux, num_attribute_streams + 1);
	encoded.mesh.mesh_style = style;

	// Compute bounds
	encode_bounds(encoded.bounds, out_meshlets.data(), out_meshlets.size(),
	              out_index_buffer.data(), position_buffer.data(), positions.size(),
	              1);

	sort_bounds(encoded.bounds.data(), encoded.bounds.size(),
	            out_meshlets.data(), encoded.mesh.meshlets.data());

	encode_bounds(encoded.bounds, out_meshlets.data(), out_meshlets.size(),
	              out_index_buffer.data(), position_buffer.data(), positions.size(),
				  ChunkFactor);

	LOGI("Exported meshlet:\n");
	LOGI("  %zu meshlets\n", encoded.mesh.meshlets.size());
	LOGI("  %zu payload bytes\n", encoded.payload.size() * sizeof(PayloadWord));
	LOGI("  %u total indices\n", mesh.count);
	LOGI("  %zu total attributes\n", mesh.positions.size() / mesh.position_stride);

	size_t uncompressed_bytes = mesh.indices.size();
	uncompressed_bytes += mesh.positions.size();
	if (style != MeshStyle::Wireframe)
		uncompressed_bytes += mesh.attributes.size();

	LOGI("  %zu uncompressed bytes\n\n\n", uncompressed_bytes);

	return export_encoded_mesh(path, encoded);
}
}
}
