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

#include "gltf.hpp"
#include "vulkan_headers.hpp"
#include "filesystem.hpp"
#include "mesh.hpp"
#include <unordered_map>
#include <algorithm>
#include "rapidjson_wrapper.hpp"
#include "muglm/matrix_helper.hpp"

using namespace std;
using namespace rapidjson;
using namespace Granite;
using namespace Util;

namespace GLTF
{

Parser::Buffer Parser::read_buffer(const string &path, uint64_t length)
{
	auto file = Global::filesystem()->open(path);
	if (!file)
		throw runtime_error("Failed to open GLTF buffer.");

	if (file->get_size() != length)
		throw runtime_error("Size mismatch of buffer.");

	void *mapped = file->map();
	if (!mapped)
		throw runtime_error("Failed to map file.");

	Buffer buf(length);
	memcpy(buf.data(), mapped, length);
	return buf;
}

Parser::Buffer Parser::read_base64(const char *data, uint64_t length)
{
	Buffer buf(length);
	auto *ptr = buf.data();

	const auto base64_index = [](char c) -> uint32_t {
		if (c >= 'A' && c <= 'Z')
			return uint32_t(c - 'A');
		else if (c >= 'a' && c <= 'z')
			return uint32_t(c - 'a') + 26;
		else if (c >= '0' && c <= '9')
			return uint32_t(c - '0') + 52;
		else if (c == '+')
			return 62;
		else if (c == '/')
			return 63;
		else
			return 0;
	};

	for (uint64_t i = 0; i < length; )
	{
		char c0 = *data++;
		if (c0 == '\0')
			break;
		char c1 = *data++;
		if (c1 == '\0')
			break;
		char c2 = *data++;
		if (c2 == '\0')
			break;
		char c3 = *data++;
		if (c3 == '\0')
			break;

		uint32_t values =
			(base64_index(c0) << 18) |
			(base64_index(c1) << 12) |
			(base64_index(c2) << 6) |
			(base64_index(c3) << 0);

		unsigned outbytes = 3;
		if (c2 == '=' && c3 == '=')
		{
			outbytes = 1;
			*ptr++ = uint8_t(values >> 16);
		}
		else if (c3 == '=')
		{
			outbytes = 2;
			*ptr++ = uint8_t(values >> 16);
			*ptr++ = uint8_t(values >> 8);
		}
		else
		{
			*ptr++ = uint8_t(values >> 16);
			*ptr++ = uint8_t(values >> 8);
			*ptr++ = uint8_t(values >> 0);
		}

		i += outbytes;
	}

	return buf;
}

Parser::Parser(const std::string &path)
{
	string json;

	{
		auto file = Global::filesystem()->open(path, FileMode::ReadOnly);
		if (!file)
			throw runtime_error("Failed to load GLTF file.");

		auto size = file->get_size();
		void *mapped = file->map();
		if (!mapped)
			throw runtime_error("Failed to map GLTF file.");

		bool is_glb = false;

		if (size >= 12 && memcmp("glTF", mapped, 4) == 0)
			is_glb = true;

		if (is_glb)
		{
			// GLB is little endian. Just parse it lazily.
			auto *words = static_cast<const uint32_t *>(mapped);
			if (words[1] != 2)
				throw runtime_error("GLB version is not 2.");
			if (words[2] > size)
				throw runtime_error("GLB length is larger than the file size.");

			auto glb_size = words[2];
			words += 3;

			auto json_length = words[0];
			if (memcmp(&words[1], "JSON", 4) != 0)
				throw runtime_error("Could not find JSON chunk.");
			words += 2;

			if (json_length + 12 > glb_size)
				throw logic_error("Header error, JSON chunk lengths out of range.");

			json = string(reinterpret_cast<const char *>(words),
			              reinterpret_cast<const char *>(words) + json_length);
			words += (json_length + 3) >> 2;

			// If there is another chunk, it's BIN chunk.
			if (json_length + 12 + 8 < glb_size)
			{
				auto binary_length = words[0];
				if (memcmp(&words[1], "BIN\0", 4) != 0)
					throw runtime_error("Could not find BIN chunk.");
				words += 2;

				if (((binary_length + 3) & ~3) + ((json_length + 3) & ~3) + (2 * 2 + 3) * sizeof(uint32_t) != glb_size)
					throw logic_error(
							"Header error, binary chunk and JSON chunk lengths do not match up with GLB size.");

				// The first buffer in the JSON must be this embedded buffer.
				Buffer buffer(reinterpret_cast<const uint8_t *>(words),
				              reinterpret_cast<const uint8_t *>(words) + binary_length);
				json_buffers.push_back(move(buffer));
			}
		}
		else
			json = string(static_cast<const char *>(mapped), static_cast<const char *>(mapped) + size);
	}
	parse(path, json);
}

#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406
#define GL_HALF_FLOAT                     0x140B
#define GL_INT_2_10_10_10_REV             0x8D9F
#define GL_UNSIGNED_INT_2_10_10_10_REV    0x8368

#define GL_REPEAT                         0x2901
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_NEAREST_MIPMAP_NEAREST         0x2700
#define GL_LINEAR_MIPMAP_NEAREST          0x2701
#define GL_NEAREST_MIPMAP_LINEAR          0x2702
#define GL_LINEAR_MIPMAP_LINEAR           0x2703

VkFormat Parser::components_to_padded_format(ScalarType type, uint32_t components)
{
	switch (type)
	{
	case ScalarType::Int8:
	{
		static const VkFormat formats[] = { VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8A8_SINT, VK_FORMAT_R8G8B8A8_SINT };
		return formats[components - 1];
	}
	case ScalarType::Int8Snorm:
	{
		static const VkFormat formats[] = { VK_FORMAT_R8_SNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8B8A8_SNORM, VK_FORMAT_R8G8B8A8_SNORM };
		return formats[components - 1];
	}
	case ScalarType::Uint8:
	{
		static const VkFormat formats[] = { VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_R8G8B8A8_UINT };
		return formats[components - 1];
	}
	case ScalarType::Uint8Unorm:
	{
		static const VkFormat formats[] = { VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
		return formats[components - 1];
	}
	case ScalarType::Int16:
	{
		static const VkFormat formats[] = { VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16A16_SINT, VK_FORMAT_R16G16B16A16_SINT };
		return formats[components - 1];
	}
	case ScalarType::Int16Snorm:
	{
		static const VkFormat formats[] = { VK_FORMAT_R16_SNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16B16A16_SNORM, VK_FORMAT_R16G16B16A16_SNORM };
		return formats[components - 1];
	}
	case ScalarType::Uint16:
	{
		static const VkFormat formats[] = { VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16A16_UINT, VK_FORMAT_R16G16B16A16_UINT };
		return formats[components - 1];
	}
	case ScalarType::Uint16Unorm:
	{
		static const VkFormat formats[] = { VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R16G16B16A16_UNORM };
		return formats[components - 1];
	}
	case ScalarType::Int32:
	{
		static const VkFormat formats[] = { VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT };
		return formats[components - 1];
	}
	case ScalarType::Uint32:
	{
		static const VkFormat formats[] = { VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT };
		return formats[components - 1];
	}
	case ScalarType::Float32:
	{
		static const VkFormat formats[] = { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };
		return formats[components - 1];
	}
	case ScalarType::Float16:
	{
		static const VkFormat formats[] = { VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT };
		return formats[components - 1];
	}

	case ScalarType::A2Bgr10Int:
	{
		if (components != 4)
			return VK_FORMAT_UNDEFINED;
		return VK_FORMAT_A2B10G10R10_SINT_PACK32;
	}

	case ScalarType::A2Bgr10Snorm:
	{
		if (components != 4)
			return VK_FORMAT_UNDEFINED;
		return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
	}

	case ScalarType::A2Bgr10Uint:
	{
		if (components != 4)
			return VK_FORMAT_UNDEFINED;
		return VK_FORMAT_A2B10G10R10_UINT_PACK32;
	}

	case ScalarType::A2Bgr10Unorm:
	{
		if (components != 4)
			return VK_FORMAT_UNDEFINED;
		return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	}

	default:
		return VK_FORMAT_UNDEFINED;
	}
}

uint32_t Parser::type_stride(ScalarType type)
{
	switch (type)
	{
	case ScalarType::Int8:
	case ScalarType::Uint8:
	case ScalarType::Int8Snorm:
	case ScalarType::Uint8Unorm:
		return 1;

	case ScalarType::Int16:
	case ScalarType::Float16:
	case ScalarType::Uint16:
	case ScalarType::Int16Snorm:
	case ScalarType::Uint16Unorm:
		return 2;

	case ScalarType::Int32:
	case ScalarType::Uint32:
	case ScalarType::Float32:
		return 4;

	case ScalarType::A2Bgr10Int:
	case ScalarType::A2Bgr10Snorm:
	case ScalarType::A2Bgr10Uint:
	case ScalarType::A2Bgr10Unorm:
		return 1; // Will be multiplied by 4.

	default:
		return 0;
	}
}

void Parser::resolve_component_type(uint32_t component_type, const char *type, bool normalized,
                                    ScalarType &scalar_type, uint32_t &components, uint32_t &stride)
{
	if (!strcmp(type, "SCALAR"))
		components = 1;
	else if (!strcmp(type, "VEC2"))
		components = 2;
	else if (!strcmp(type, "VEC3"))
		components = 3;
	else if (!strcmp(type, "VEC4"))
		components = 4;
	else if (!strcmp(type, "MAT3"))
		components = 9;
	else if (!strcmp(type, "MAT4"))
		components = 16;
	else
		throw logic_error("Unknown component type.");

	switch (component_type)
	{
	case GL_BYTE:
	{
		scalar_type = normalized ? ScalarType::Int8Snorm : ScalarType::Int8;
		break;
	}

	case GL_UNSIGNED_BYTE:
	{
		scalar_type = normalized ? ScalarType::Uint8Unorm : ScalarType::Uint8;
		break;
	}

	case GL_SHORT:
	{
		scalar_type = normalized ? ScalarType::Int16Snorm : ScalarType::Int16;
		break;
	}

	case GL_UNSIGNED_SHORT:
	{
		scalar_type = normalized ? ScalarType::Uint16Unorm : ScalarType::Uint16;
		break;
	}

	case GL_INT:
	{
		scalar_type = ScalarType::Int32;
		break;
	}

	case GL_UNSIGNED_INT:
	{
		scalar_type = ScalarType::Uint32;
		break;
	}

	case GL_FLOAT:
	{
		scalar_type = ScalarType::Float32;
		break;
	}

	case GL_HALF_FLOAT:
	{
		scalar_type = ScalarType::Float16;
		break;
	}

	case GL_INT_2_10_10_10_REV:
	{
		scalar_type = normalized ? ScalarType::A2Bgr10Snorm : ScalarType::A2Bgr10Int;
		break;
	}

	case GL_UNSIGNED_INT_2_10_10_10_REV:
	{
		scalar_type = normalized ? ScalarType::A2Bgr10Unorm : ScalarType::A2Bgr10Uint;
		break;
	}

	default:
		throw logic_error("Unknown type.");
	}

	stride = components * type_stride(scalar_type);
}

template <typename T>
static void iterate_elements(const Value &value, const T &t)
{
	if (value.IsArray())
	{
		for (auto itr = value.Begin(); itr != value.End(); ++itr)
		{
			t(*itr);
		}
	}
}

template <typename T, typename Func>
static void reiterate_elements(T *nodes, const Value &value, const Func &func)
{
	if (value.IsArray())
	{
		for (auto itr = value.Begin(); itr != value.End(); ++itr, nodes++)
			func(*nodes, *itr);
	}
	else
	{
		for (auto itr = value.MemberBegin(); itr != value.MemberEnd(); ++itr, nodes++)
			func(*nodes, itr->value);
	}
}

template <typename T>
static void read_min_max(T &out, ScalarType type, const Value &v)
{
	switch (type)
	{
	case ScalarType::Float32:
	case ScalarType::Float16:
	case ScalarType::A2Bgr10Snorm:
	case ScalarType::A2Bgr10Unorm:
	case ScalarType::Int8Snorm:
	case ScalarType::Int16Snorm:
	case ScalarType::Uint8Unorm:
	case ScalarType::Uint16Unorm:
		out.f32 = v.GetFloat();
		break;

	case ScalarType::Int8:
	case ScalarType::Int16:
	case ScalarType::Int32:
	case ScalarType::A2Bgr10Int:
		out.i32 = v.GetInt();
		break;

	case ScalarType::Uint8:
	case ScalarType::Uint16:
	case ScalarType::Uint32:
	case ScalarType::A2Bgr10Uint:
		out.u32 = v.GetUint();
		break;
	}
}

static MeshAttribute semantic_to_attribute(const char *semantic)
{
	if (!strcmp(semantic, "POSITION"))
		return MeshAttribute::Position;
	else if (!strcmp(semantic, "NORMAL"))
		return MeshAttribute::Normal;
	else if (!strcmp(semantic, "TEXCOORD_0"))
		return MeshAttribute::UV;
	else if (!strcmp(semantic, "TANGENT"))
		return MeshAttribute::Tangent;
	else if (!strcmp(semantic, "JOINTS_0"))
		return MeshAttribute::BoneIndex;
	else if (!strcmp(semantic, "WEIGHTS_0"))
		return MeshAttribute::BoneWeights;
	else if (!strcmp(semantic, "COLOR_0"))
		return MeshAttribute::VertexColor;
	else if (!strcmp(semantic, "TEXCOORD_1"))
		return MeshAttribute::None; // Ignore
	else
		throw logic_error("Unsupported semantic.");
}

static VkPrimitiveTopology gltf_topology(const char *top)
{
	if (!strcmp(top, "TRIANGLES"))
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	else if (!strcmp(top, "TRIANGLE_STRIP"))
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	else if (!strcmp(top, "TRIANGLE_FAN"))
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
	else if (!strcmp(top, "POINTS"))
		return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	else if (!strcmp(top, "LINES"))
		return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	else if (!strcmp(top, "LINE_STRIP"))
		return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
	else
		throw logic_error("Unrecognized primitive mode.");
}

void Parser::extract_attribute(std::vector<float> &attributes, const Accessor &accessor)
{
	if (accessor.type != ScalarType::Float32)
		throw logic_error("Attribute is not Float32.");
	if (accessor.components != 1)
		throw logic_error("Attribute is not single component.");

	auto &view = json_views[accessor.view];
	auto &buffer = json_buffers[view.buffer_index];
	for (uint32_t i = 0; i < accessor.count; i++)
	{
		uint32_t offset = view.offset + accessor.offset + i * accessor.stride;
		const auto *data = reinterpret_cast<const float *>(&buffer[offset]);
		attributes.push_back(*data);
	}
}

void Parser::extract_attribute(std::vector<vec3> &attributes, const Accessor &accessor)
{
	if (accessor.type != ScalarType::Float32)
		throw logic_error("Attribute is not Float32.");
	if (accessor.components != 3)
		throw logic_error("Attribute is not single component.");

	auto &view = json_views[accessor.view];
	auto &buffer = json_buffers[view.buffer_index];
	for (uint32_t i = 0; i < accessor.count; i++)
	{
		uint32_t offset = view.offset + accessor.offset + i * accessor.stride;
		const auto *data = reinterpret_cast<const float *>(&buffer[offset]);
		attributes.push_back(vec3(data[0], data[1], data[2]));
	}
}

void Parser::extract_attribute(std::vector<quat> &attributes, const Accessor &accessor)
{
	if (accessor.type != ScalarType::Float32)
		throw logic_error("Attribute is not Float32.");
	if (accessor.components != 4)
		throw logic_error("Attribute is not single component.");

	auto &view = json_views[accessor.view];
	auto &buffer = json_buffers[view.buffer_index];
	for (uint32_t i = 0; i < accessor.count; i++)
	{
		uint32_t offset = view.offset + accessor.offset + i * accessor.stride;
		const auto *data = reinterpret_cast<const float *>(&buffer[offset]);
		attributes.push_back(normalize(quat(data[3], data[0], data[1], data[2])));
	}
}

void Parser::extract_attribute(std::vector<mat4> &attributes, const Accessor &accessor)
{
	if (accessor.type != ScalarType::Float32)
		throw logic_error("Attribute is not Float32.");
	if (accessor.components != 16)
		throw logic_error("Attribute is not single component.");

	auto &view = json_views[accessor.view];
	auto &buffer = json_buffers[view.buffer_index];
	for (uint32_t i = 0; i < accessor.count; i++)
	{
		uint32_t offset = view.offset + accessor.offset + i * accessor.stride;
		const auto *data = reinterpret_cast<const float *>(&buffer[offset]);
		attributes.push_back(mat4(
			vec4(data[0], data[1], data[2], data[3]),
			vec4(data[4], data[5], data[6], data[7]),
			vec4(data[8], data[9], data[10], data[11]),
			vec4(data[12], data[13], data[14], data[15])));
	}
}

static void build_bone_hierarchy(Skin::Bone &bone, const vector<vector<uint32_t>> &hierarchy, uint32_t index)
{
	for (auto &child : hierarchy[index])
	{
		Skin::Bone child_bone;
		child_bone.index = child;
		build_bone_hierarchy(child_bone, hierarchy, child);
		bone.children.push_back(move(child_bone));
	}
}

void Parser::parse(const string &original_path, const string &json)
{
	Document doc;
	doc.Parse(json);

	if (doc.HasParseError())
		throw logic_error("Parser error found.");

	const auto add_buffer = [&](const Value &buf) {
		const char *uri = nullptr;
		if (buf.HasMember("uri"))
			uri = buf["uri"].GetString();

		auto length = buf["byteLength"].GetUint64();

		if (!uri)
		{
			//if (length != json_buffers.front().size())
			//	throw logic_error("Baked GLB buffer size must match the provided size in the header.");
			return;
		}

		static const char base64_type[] = "data:application/octet-stream;base64,";
		if (!strncmp(uri, base64_type, strlen(base64_type)))
		{
			json_buffers.push_back(read_base64(uri + strlen(base64_type), length));
		}
		else
		{
			auto path = Path::relpath(original_path, uri);
			json_buffers.push_back(read_buffer(path, length));
		}
	};

	const auto add_view = [&](const Value &view) {
		auto &buf = view["buffer"];
		auto buffer_index = buf.GetUint();
		auto offset = view.HasMember("byteOffset") ? view["byteOffset"].GetUint() : 0u;
		auto length = view["byteLength"].GetUint();

		if (offset + length > json_buffers[buffer_index].size())
			throw logic_error("Buffer view is out of range.");

		auto stride = view.HasMember("byteStride") ? view["byteStride"].GetUint() : 0u;
		json_views.push_back({buffer_index, offset, length, stride});
	};

	const auto add_accessor = [&](const Value &accessor) {
		auto &view = accessor["bufferView"];
		auto view_index = view.GetUint();

		uint32_t offset = 0;
		if (accessor.HasMember("byteOffset"))
			offset = accessor["byteOffset"].GetUint();
		auto component_type = accessor["componentType"].GetUint();
		auto count = accessor["count"].GetUint();
		auto *type = accessor["type"].GetString();
		bool normalized = false;
		if (accessor.HasMember("normalized"))
			normalized = accessor["normalized"].GetBool();

		Accessor acc = {};
		resolve_component_type(component_type, type, normalized, acc.type, acc.components, acc.stride);
		acc.view = view_index;
		acc.offset = offset;
		acc.count = count;

		if (json_views[view_index].stride)
			acc.stride = json_views[view_index].stride;

		auto *minimums = acc.min;
		if (accessor.HasMember("min"))
		{
			for (auto itr = accessor["min"].Begin(); itr != accessor["min"].End(); ++itr)
			{
				assert(minimums - acc.min < 16);
				read_min_max(*minimums, acc.type, *itr);
				minimums++;
			}
		}

		auto *maximums = acc.max;
		if (accessor.HasMember("max"))
		{
			for (auto itr = accessor["max"].Begin(); itr != accessor["max"].End(); ++itr)
			{
				assert(maximums - acc.max < 16);
				read_min_max(*maximums, acc.type, *itr);
				maximums++;
			}
		}

		json_accessors.push_back(acc);
	};

	const auto parse_primitive = [&](const Value &primitive) -> MeshData::AttributeData {
		MeshData::AttributeData attr = {};
		if (primitive.HasMember("indices"))
		{
			attr.index_buffer.active = true;
			auto &indices = primitive["indices"];
			attr.index_buffer.accessor_index = indices.GetUint();
		}

		if (primitive.HasMember("material"))
		{
			auto &mat = primitive["material"];
			attr.material_index = mat.GetUint();
			attr.has_material = true;
		}
		else
			attr.has_material = false;

		attr.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		if (primitive.HasMember("mode"))
		{
			auto &top = primitive["mode"];
			if (top.IsString())
				attr.topology = gltf_topology(top.GetString());
			else
			{
				static const VkPrimitiveTopology topologies[] = {
					VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
				    VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
					VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, // Loop not supported in Vulkan it seems.
					VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
					VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
					VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
					VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
				};
				attr.topology = topologies[top.GetUint()];
			};
		}

		attr.primitive_restart = false;
		if (primitive.HasMember("extras"))
		{
			auto &extras = primitive["extras"];
			if (extras.HasMember("primitiveRestart"))
				attr.primitive_restart = extras["primitiveRestart"].GetBool();
		}

		auto &attrs = primitive["attributes"];
		for (auto itr = attrs.MemberBegin(); itr != attrs.MemberEnd(); ++itr)
		{
			auto *semantic = itr->name.GetString();
			uint32_t accessor_index = itr->value.GetUint();
			MeshAttribute attribute = semantic_to_attribute(semantic);

			if (attribute != MeshAttribute::None)
			{
				attr.attributes[ecast(attribute)].accessor_index = accessor_index;
				attr.attributes[ecast(attribute)].active = true;
			}
		}

		return attr;
	};

	const auto add_mesh = [&](const Value &mesh) {
		auto &prims = mesh["primitives"];
		MeshData data;
		for (auto itr = prims.Begin(); itr != prims.End(); ++itr)
			data.primitives.push_back(parse_primitive(*itr));
		json_meshes.push_back(move(data));
	};

	const auto add_image = [&](const Value &image) {
		if (image.HasMember("bufferView"))
		{
			auto index = image["bufferView"].GetUint();
			auto &view = json_views[index];
			auto fake_path = string("memory://") + original_path + "_buffer_view_" + to_string(index);

			auto file = Global::filesystem()->open(fake_path, FileMode::WriteOnly);
			if (!file)
				throw runtime_error("Failed to open memory file.");

			void *mapped = file->map_write(view.length);
			if (!mapped)
				throw runtime_error("Failed to map memory file.");

			memcpy(mapped, json_buffers[view.buffer_index].data() + view.offset, view.length);
			json_images.emplace_back(move(fake_path));
		}
		else
		{
			auto *uri = image["uri"].GetString();
			static const char base64_type_jpg[] = "data:image/jpeg;base64,";
			static const char base64_type_png[] = "data:image/png;base64,";
			const char *base64_data = nullptr;

			if (!strncmp(uri, base64_type_jpg, strlen(base64_type_jpg)))
				base64_data = uri + strlen(base64_type_jpg);
			else if (!strncmp(uri, base64_type_png, strlen(base64_type_png)))
				base64_data = uri + strlen(base64_type_png);
			else
				json_images.emplace_back(Path::relpath(original_path, image["uri"].GetString()));

			if (base64_data)
			{
				auto str_length = strlen(base64_data);
				auto data_length = 3 * (str_length >> 2);
				if (base64_data[str_length - 1] == '=')
					data_length--;
				if (base64_data[str_length - 2] == '=')
					data_length--;

				auto base64_buffer = read_base64(uri + strlen(base64_type_jpg), data_length);
				auto fake_path = string("memory://") + original_path + "_base64_" + to_string(json_images.size());

				auto file = Global::filesystem()->open(fake_path, FileMode::WriteOnly);
				if (!file)
					throw runtime_error("Failed to open memory file.");

				void *mapped = file->map_write(data_length);
				if (!mapped)
					throw runtime_error("Failed to map memory file.");

				memcpy(mapped, base64_buffer.data(), base64_buffer.size());
				json_images.emplace_back(move(fake_path));
			}
		}
	};

	const auto add_stock_sampler = [&](const Value &value) {
		unsigned wrap_s = GL_REPEAT;
		unsigned wrap_t = GL_REPEAT;
		unsigned min_filter = GL_LINEAR_MIPMAP_LINEAR;
		unsigned mag_filter = GL_LINEAR;

		if (value.HasMember("magFilter"))
			mag_filter = value["magFilter"].GetUint();
		if (value.HasMember("minFilter"))
			min_filter = value["minFilter"].GetUint();
		if (value.HasMember("wrapS"))
			wrap_s = value["wrapS"].GetUint();
		if (value.HasMember("wrapT"))
			wrap_t = value["wrapT"].GetUint();

		Vulkan::StockSampler sampler = Vulkan::StockSampler::TrilinearWrap;

		struct Entry
		{
			unsigned wrap_s, wrap_t, mag_filter, min_filter;
			Vulkan::StockSampler sampler;
		};
		static const Entry entries[] = {
			{ GL_REPEAT, GL_REPEAT, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR, Vulkan::StockSampler::TrilinearWrap },
			{ GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR, Vulkan::StockSampler::TrilinearClamp },
			{ GL_REPEAT, GL_REPEAT, GL_LINEAR, GL_LINEAR_MIPMAP_NEAREST, Vulkan::StockSampler::LinearWrap },
			{ GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR_MIPMAP_NEAREST, Vulkan::StockSampler::LinearClamp },
			{ GL_REPEAT, GL_REPEAT, GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST, Vulkan::StockSampler::NearestWrap },
			{ GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST, Vulkan::StockSampler::NearestClamp },
		};

		auto itr = find_if(begin(entries), end(entries), [&](const Entry &e) {
			return e.wrap_s == wrap_s && e.wrap_t == wrap_t && e.min_filter == min_filter && e.mag_filter == mag_filter;
		});

		if (itr != end(entries))
			sampler = itr->sampler;
		else
			LOGE("Could not find stock sampler, using TrilinearWrap.\n");

		json_stock_samplers.push_back(sampler);
	};

	const auto add_texture = [&](const Value &value) {
		auto &source = value["source"];

		Vulkan::StockSampler stock_sampler = Vulkan::StockSampler::TrilinearWrap;
		if (value.HasMember("sampler"))
		{
			auto &sampler = value["sampler"];
			stock_sampler = json_stock_samplers[sampler.GetUint()];
		}
		json_textures.push_back({ source.GetUint(), stock_sampler });
	};

	const auto add_material = [&](const Value &value) {
		MaterialInfo info;

		info.uniform_base_color = vec4(1.0f);
		info.uniform_roughness = 1.0f;
		info.uniform_metallic = 1.0f;
		info.two_sided = false;
		if (value.HasMember("doubleSided"))
			info.two_sided = value["doubleSided"].GetBool();

		info.pipeline = DrawPipeline::Opaque;
		if (value.HasMember("alphaMode"))
		{
			string mode = value["alphaMode"].GetString();
			if (mode == "OPAQUE")
				info.pipeline = DrawPipeline::Opaque;
			else if (mode == "MASK")
				info.pipeline = DrawPipeline::AlphaTest;
			else if (mode == "BLEND")
				info.pipeline = DrawPipeline::AlphaBlend;
		}

		if (value.HasMember("extras"))
		{
			auto &extras = value["extras"];
			if (extras.HasMember("bandlimitedPixel"))
				info.bandlimited_pixel = extras["bandlimitedPixel"].GetBool();
		}

		if (value.HasMember("emissiveFactor"))
		{
			auto &e = value["emissiveFactor"];
			info.uniform_emissive_color = vec3(e[0].GetFloat(), e[1].GetFloat(), e[2].GetFloat());
		}

		if (value.HasMember("normalTexture"))
		{
			auto &tex = value["normalTexture"]["index"];
			auto &image = json_images[json_textures[tex.GetUint()].image_index];
			info.normal = image;
			if (value["normalTexture"].HasMember("scale"))
				info.normal_scale = value["normalTexture"]["scale"].GetFloat();
		}

		if (value.HasMember("emissiveTexture"))
		{
			auto &tex = value["emissiveTexture"]["index"];
			auto &image = json_images[json_textures[tex.GetUint()].image_index];
			info.emissive = image;
		}

		if (value.HasMember("occlusionTexture"))
		{
			auto &tex = value["occlusionTexture"]["index"];
			auto &image = json_images[json_textures[tex.GetUint()].image_index];
			info.occlusion = image;
		}

		if (value.HasMember("extensions"))
		{
			auto &ext = value["extensions"];
			for (auto itr = ext.MemberBegin(); itr != ext.MemberEnd(); ++itr)
			{
				if (itr->name == "KHR_materials_pbrSpecularGlossiness")
				{
					auto &pbr_value = itr->value;
					if (pbr_value.HasMember("diffuseFactor"))
					{
						auto &diff = pbr_value["diffuseFactor"];
						info.uniform_base_color = vec4(diff[0].GetFloat(), diff[1].GetFloat(),
						                               diff[2].GetFloat(), diff[3].GetFloat());
					}

					if (pbr_value.HasMember("glossinessFactor"))
					{
						auto &gloss = pbr_value["glossinessFactor"];
						// Probably some remapping needed.
						info.uniform_roughness = clamp(1.0f - gloss.GetFloat(), 0.0f, 1.0f);
					}

					if (pbr_value.HasMember("specularFactor"))
					{
						auto &spec = pbr_value["specularFactor"];
						// No idea how to remap ...
						info.uniform_metallic = muglm::max(muglm::max(spec[0].GetFloat(), spec[1].GetFloat()), spec[2].GetFloat());
					}

					if (pbr_value.HasMember("diffuseTexture"))
					{
						auto &tex = pbr_value["diffuseTexture"]["index"];
						info.base_color = json_images[json_textures[tex.GetUint()].image_index];
						info.sampler = json_textures[tex.GetUint()].sampler;
					}

					if (pbr_value.HasMember("specularGlossinessTexture"))
					{
						LOGE("Specular glossiness texture not supported, use PBR!\n");
					}
				}
			}
		}

		if (value.HasMember("pbrMetallicRoughness"))
		{
			auto &mr = value["pbrMetallicRoughness"];
			if (mr.HasMember("baseColorTexture"))
			{
				auto &tex = mr["baseColorTexture"]["index"];
				info.base_color = json_images[json_textures[tex.GetUint()].image_index];
				info.sampler = json_textures[tex.GetUint()].sampler;
			}

			if (mr.HasMember("metallicRoughnessTexture"))
			{
				auto &tex = mr["metallicRoughnessTexture"]["index"];
				info.metallic_roughness = json_images[json_textures[tex.GetUint()].image_index];
			}

			if (mr.HasMember("baseColorFactor"))
			{
				auto &v = mr["baseColorFactor"];
				info.uniform_base_color = vec4(v[0].GetFloat(), v[1].GetFloat(), v[2].GetFloat(), v[3].GetFloat());
			}

			if (mr.HasMember("metallicFactor"))
				info.uniform_metallic = mr["metallicFactor"].GetFloat();
			if (mr.HasMember("roughnessFactor"))
				info.uniform_roughness = mr["roughnessFactor"].GetFloat();

			// Workaround many broken materials ...
			if (!mr.HasMember("metallicRoughnessTexture") && !mr.HasMember("metallicFactor"))
				info.uniform_metallic = 0.0f;
		}

		materials.push_back(move(info));
	};

	const auto add_node = [&](const Value &value) {
		Node node;

		if (value.HasMember("mesh"))
		{
			auto &m = value["mesh"];
			auto index = m.GetUint();
			for (auto &prim : mesh_index_to_primitives[index])
				node.meshes.push_back(prim);
		}

		if (value.HasMember("camera"))
		{
			auto index = value["camera"].GetUint();
			json_cameras[index].node_index = uint32_t(nodes.size());
			json_cameras[index].attached_to_node = true;
		}

		if (value.HasMember("extensions"))
		{
			auto &ext = value["extensions"];
			if (ext.HasMember("KHR_lights_punctual"))
			{
				auto &cmn = ext["KHR_lights_punctual"];
				if (cmn.HasMember("light"))
				{
					auto index = cmn["light"].GetUint();
					json_lights[index].node_index = uint32_t(nodes.size());
					json_lights[index].attached_to_node = true;
				}
			}
		}

		if (value.HasMember("skin"))
		{
			auto &s = value["skin"];
			node.has_skin = true;
			node.skin = s.GetUint();
		}

		if (value.HasMember("children"))
		{
			auto &children = value["children"];
			for (auto itr = children.Begin(); itr != children.End(); ++itr)
				node.children.push_back(itr->GetUint());
		}

		if (value.HasMember("meshes"))
		{
			auto &m = value["meshes"];
			for (auto itr = m.Begin(); itr != m.End(); ++itr)
			{
				auto index = itr->GetUint();
				for (auto &prim : mesh_index_to_primitives[index])
					node.meshes.push_back(prim);
			}
		}

		if (value.HasMember("translation"))
		{
			auto &t = value["translation"];
			node.transform.translation = vec3(t[0].GetFloat(), t[1].GetFloat(), t[2].GetFloat());
		}

		if (value.HasMember("rotation"))
		{
			auto &r = value["rotation"];
			node.transform.rotation = normalize(quat(r[3].GetFloat(), r[0].GetFloat(), r[1].GetFloat(), r[2].GetFloat()));
		}

		if (value.HasMember("scale"))
		{
			auto &s = value["scale"];
			node.transform.scale = vec3(s[0].GetFloat(), s[1].GetFloat(), s[2].GetFloat());
		}

		if (value.HasMember("matrix"))
		{
			auto &m = value["matrix"];
			mat4 transform(
					vec4(m[0].GetFloat(), m[1].GetFloat(), m[2].GetFloat(), m[3].GetFloat()),
					vec4(m[4].GetFloat(), m[5].GetFloat(), m[6].GetFloat(), m[7].GetFloat()),
					vec4(m[8].GetFloat(), m[9].GetFloat(), m[10].GetFloat(), m[11].GetFloat()),
					vec4(m[12].GetFloat(), m[13].GetFloat(), m[14].GetFloat(), m[15].GetFloat()));

			// Decompose transform into TRS. Spec says this must be possible.
			decompose(transform, node.transform.scale, node.transform.rotation, node.transform.translation);
		}

		nodes.push_back(move(node));
	};

	const auto add_skin = [&](const Value &skin) {
		Util::Hasher hasher;

		auto &joints = skin["joints"];
		vector<NodeTransform> joint_transforms;
		vector<uint32_t> joint_indices;

		vector<int> parents(joints.GetArray().Size());
		for (auto &p : parents)
			p = -1;

		vector<vector<uint32_t>> hierarchy(joints.GetArray().Size());
		joint_transforms.reserve(joints.GetArray().Size());
		joint_indices.reserve(joints.GetArray().Size());

		hasher.u32(joints.GetArray().Size());
		for (auto itr = joints.Begin(); itr != joints.End(); ++itr)
		{
			uint32_t joint_index = itr->GetUint();
			joint_indices.push_back(joint_index);
			if (json_node_index_to_joint_index.find(joint_index) != end(json_node_index_to_joint_index))
				throw logic_error("A joint cannot be attached to multiple skins.");
			if (json_node_index_to_skin.find(joint_index) != end(json_node_index_to_skin))
				throw logic_error("A joint cannot be attached to multiple skins.");

			json_node_index_to_skin[joint_index] = json_skins.size();
			json_node_index_to_joint_index[joint_index] = joint_transforms.size();
			hasher.u32(joint_index);

			auto &node = nodes[joint_index];
			node.joint = true;
			joint_transforms.push_back(node.transform);
		}

		for (unsigned i = 0; i < joint_indices.size(); i++)
		{
			uint32_t joint_index = joint_indices[i];
			auto &node = nodes[joint_index];

			for (auto &child : node.children)
			{
				auto itr = json_node_index_to_joint_index.find(child);
				if (itr == end(json_node_index_to_joint_index))
					throw logic_error("Joint has a child which is not part of the skeleton.");
				uint32_t index = itr->second;

				if (parents[index] != -1)
					throw logic_error("Joint cannot have two parents.");
				parents[index] = i;
				hierarchy[i].push_back(index);
			}
		}

		vector<Skin::Bone> skeleton;
		for (unsigned i = 0; i < parents.size(); i++)
		{
			if (parents[i] == -1)
			{
				// This is a top-level node in the skeleton hierarchy.
				Skin::Bone bone;
				bone.index = i;
				build_bone_hierarchy(bone, hierarchy, i);
				skeleton.push_back(move(bone));
			}
		}

		std::vector<mat4> inverse_bind_matrices;
		inverse_bind_matrices.reserve(joint_transforms.size());

		if (skin.HasMember("inverseBindMatrices"))
		{
			uint32_t accessor = skin["inverseBindMatrices"].GetUint();
			extract_attribute(inverse_bind_matrices, json_accessors[accessor]);
		}
		else
		{
			for (uint32_t i = 0; i < joints.GetArray().Size(); i++)
				inverse_bind_matrices.push_back(mat4(1.0f));
		}

		auto compat = hasher.get();
		skin_compat.push_back(compat);
		json_skins.push_back({ move(inverse_bind_matrices), move(joint_transforms), move(skeleton), compat });
	};

	const auto add_camera = [&](const Value &camera) {
		CameraInfo info;

		if (camera.HasMember("name"))
			info.name = camera["name"].GetString();

		if (camera.HasMember("type"))
		{
			if (strcmp(camera["type"].GetString(), "perspective") == 0)
			{
				info.type = CameraInfo::Type::Perspective;
				auto &p = camera["perspective"];
				if (p.HasMember("yfov"))
					info.yfov = p["yfov"].GetFloat();
				if (p.HasMember("znear"))
					info.znear = p["znear"].GetFloat();
				if (p.HasMember("zfar"))
					info.zfar = p["zfar"].GetFloat();
				if (p.HasMember("aspectRatio"))
					info.aspect_ratio = p["aspectRatio"].GetFloat();
			}
			else if (strcmp(camera["type"].GetString(), "orthographic") == 0)
			{
				info.type = CameraInfo::Type::Orthographic;
				auto &o = camera["orthographic"];
				if (o.HasMember("znear"))
					info.znear = o["znear"].GetFloat();
				if (o.HasMember("zfar"))
					info.zfar = o["zfar"].GetFloat();
				if (o.HasMember("xmag"))
					info.xmag = o["xmag"].GetFloat();
				if (o.HasMember("ymag"))
					info.ymag = o["ymag"].GetFloat();
			}
		}

		json_cameras.push_back(move(info));
	};

	const auto add_light = [&](const Value &light) {
		LightInfo info;

		if (light.HasMember("name"))
			info.name = light["name"].GetString();

		float intensity = 1.0f;
		if (light.HasMember("intensity"))
			intensity = light["intensity"].GetFloat();

		if (light.HasMember("color"))
		{
			auto &color = light["color"];
			info.color.x = color[0].GetFloat();
			info.color.y = color[1].GetFloat();
			info.color.z = color[2].GetFloat();
		}
		else
			info.color = vec3(1.0f);

		info.color *= intensity;

		auto *type = light["type"].GetString();
		if (strcmp(type, "point") == 0)
			info.type = LightInfo::Type::Point;
		else if (strcmp(type, "spot") == 0)
			info.type = LightInfo::Type::Spot;
		else if (strcmp(type, "directional") == 0)
			info.type = LightInfo::Type::Directional;
		else if (strcmp(type, "ambient") == 0)
			info.type = LightInfo::Type::Ambient;
		else
			throw logic_error("Invalid light type.");

		info.range = 0.0f;
		if (light.HasMember("range"))
			info.range = light["range"].GetFloat();

		info.inner_cone = std::cos(0.0f);
		info.outer_cone = std::cos(pi<float>() / 4.0f);

		if (info.type == LightInfo::Type::Spot)
		{
			if (light.HasMember("spot"))
			{
				auto &spot = light["spot"];
				if (spot.HasMember("innerConeAngle"))
				{
					info.inner_cone = spot["innerConeAngle"].GetFloat();
					info.inner_cone = std::cos(info.inner_cone);
				}

				if (spot.HasMember("outerConeAngle"))
				{
					info.outer_cone = spot["outerConeAngle"].GetFloat();
					info.outer_cone = std::cos(info.outer_cone);
				}
			}
		}

		json_lights.push_back(move(info));
	};

	const auto add_environment = [&](const Value &value) {
		MaterialInfo::Texture cube, reflection, irradiance;

		if (value.HasMember("cubeTexture"))
		{
			auto index = json_textures[value["cubeTexture"].GetUint()].image_index;
			cube = json_images[index];
		}

		if (value.HasMember("reflectionTexture"))
		{
			auto index = json_textures[value["reflectionTexture"].GetUint()].image_index;
			reflection = json_images[index];
		}

		if (value.HasMember("irradianceTexture"))
		{
			auto index = json_textures[value["irradianceTexture"].GetUint()].image_index;
			irradiance = json_images[index];
		}

		float intensity = 1.0f;
		if (value.HasMember("intensity"))
			intensity = value["intensity"].GetFloat();

		vec3 fog_color = vec3(0.0f);
		float fog_falloff = 1.0f;
		if (value.HasMember("fog"))
		{
			auto &fog = value["fog"];
			fog_color = vec3(fog["color"][0].GetFloat(), fog["color"][1].GetFloat(), fog["color"][2].GetFloat());
			fog_falloff = fog["falloff"].GetFloat();
		}

		EnvironmentInfo::Fog fog = { fog_color, fog_falloff };
		json_environments.push_back({ move(cube), move(reflection), move(irradiance), intensity, fog });
	};

	if (doc.HasMember("cameras"))
		iterate_elements(doc["cameras"], add_camera);

	if (doc.HasMember("extensions"))
	{
		auto &ext = doc["extensions"];
		if (ext.HasMember("KHR_lights_punctual") && ext["KHR_lights_punctual"].HasMember("lights"))
		{
			auto &lights = ext["KHR_lights_punctual"]["lights"];
			iterate_elements(lights, add_light);
		}
	}

	if (doc.HasMember("buffers"))
		iterate_elements(doc["buffers"], add_buffer);
	if (doc.HasMember("bufferViews"))
		iterate_elements(doc["bufferViews"], add_view);
	if (doc.HasMember("images"))
		iterate_elements(doc["images"], add_image);
	if (doc.HasMember("samplers"))
		iterate_elements(doc["samplers"], add_stock_sampler);
	if (doc.HasMember("textures"))
		iterate_elements(doc["textures"], add_texture);
	if (doc.HasMember("materials"))
		iterate_elements(doc["materials"], add_material);
	if (doc.HasMember("accessors"))
		iterate_elements(doc["accessors"], add_accessor);
	if (doc.HasMember("meshes"))
		iterate_elements(doc["meshes"], add_mesh);

	if (doc.HasMember("extras"))
	{
		auto &extra = doc["extras"];
		if (extra.HasMember("environments"))
			iterate_elements(extra["environments"], add_environment);
	}

	build_meshes();
	if (doc.HasMember("nodes"))
		iterate_elements(doc["nodes"], add_node);

	if (doc.HasMember("skins"))
		iterate_elements(doc["skins"], add_skin);

	const auto add_animation = [&](const Value &animation) {
		auto &samplers = animation["samplers"];
		auto &channels = animation["channels"];

		vector<Accessor *> json_time;
		vector<Accessor *> json_samplers;
		vector<const char *> json_interpolation;

		const auto add_sampler = [&](const Value &v) {
			auto &input = v["input"];
			auto &output = v["output"];

			json_time.push_back(&json_accessors[input.GetUint()]);
			json_samplers.push_back(&json_accessors[output.GetUint()]);
			json_interpolation.push_back(v.HasMember("interpolation") ? v["interpolation"].GetString() : "LINEAR");
		};

		iterate_elements(samplers, add_sampler);

		Animation combined_animation;

		for (auto itr = channels.Begin(); itr != channels.End(); ++itr)
		{
			auto &sampler = json_samplers[(*itr)["sampler"].GetUint()];
			auto &animation_target = (*itr)["target"];
			auto &node_id = animation_target.HasMember("node") ? animation_target["node"] : animation_target["id"];

			AnimationChannel channel;
			channel.node_index = node_id.GetUint();

			if (nodes[channel.node_index].joint)
			{
				auto joint_index_itr = json_node_index_to_joint_index.find(channel.node_index);
				if (joint_index_itr == end(json_node_index_to_joint_index))
					throw logic_error("Joint is not attached to a skeleton.");

				auto skin_itr = json_node_index_to_skin.find(channel.node_index);
				if (skin_itr == end(json_node_index_to_skin))
					throw logic_error("Joint name does not exist in a skin.");

				channel.joint_index = joint_index_itr->second;
				channel.joint = true;

				uint32_t skin_index = skin_itr->second;
				if (!combined_animation.skinning)
				{
					combined_animation.skinning = true;
					combined_animation.skin_compat = skin_compat[skin_index]; // Any node which receives this animation must have the same skin.
				}
				else if (combined_animation.skin_compat != skin_compat[skin_index])
					throw logic_error("Cannot have two different skin indices in a single animation.");
			}

			extract_attribute(channel.timestamps, *json_time[(*itr)["sampler"].GetUint()]);

			const char *target = (*itr)["target"]["path"].GetString();
			const char *interpolation = json_interpolation[(*itr)["sampler"].GetUint()];

			if (strcmp(interpolation, "LINEAR") == 0)
			{
				if (!strcmp(target, "translation"))
				{
					channel.type = AnimationChannel::Type::Translation;
					extract_attribute(channel.linear.values, *sampler);
				}
				else if (!strcmp(target, "rotation"))
				{
					channel.type = AnimationChannel::Type::Rotation;
					extract_attribute(channel.spherical.values, *sampler);
				}
				else if (!strcmp(target, "scale"))
				{
					channel.type = AnimationChannel::Type::Scale;
					extract_attribute(channel.linear.values, *sampler);
				}
				else
					throw logic_error("Invalid target for animation.");
			}
			else if (strcmp(interpolation, "CUBICSPLINE") == 0)
			{
				if (!strcmp(target, "translation"))
				{
					channel.type = AnimationChannel::Type::CubicTranslation;
					extract_attribute(channel.cubic.values, *sampler);
				}
				else if (!strcmp(target, "scale"))
				{
					channel.type = AnimationChannel::Type::CubicScale;
					extract_attribute(channel.cubic.values, *sampler);
				}
				else
					throw logic_error("Invalid target for animation.");
			}
			else
				throw logic_error("Unsupported interpolation type.");

			combined_animation.channels.push_back(move(channel));
		}
		combined_animation.update_length();
		combined_animation.name = move(json_animation_names[animations.size()]);
		animations.push_back(move(combined_animation));
	};

	if (doc.HasMember("animations"))
	{
		auto &animation_list = doc["animations"];
		unsigned counter = 0;
		for (auto itr = animation_list.Begin(); itr != animation_list.End(); ++itr)
		{
			string name;

			if (itr->HasMember("name"))
				name = (*itr)["name"].GetString();
			else
			{
				name = "animation_";
				name += to_string(counter);
			}

			json_animation_names.push_back(move(name));
			counter++;
		}
		iterate_elements(animation_list, add_animation);
	}

	if (doc.HasMember("scenes"))
	{
		auto &scenes = doc["scenes"];
		for (auto itr = scenes.Begin(); itr != scenes.End(); ++itr)
		{
			auto &s = *itr;
			SceneNodes sc;

			if (s.HasMember("name"))
				sc.name = s["name"].GetString();

			if (s.HasMember("nodes"))
			{
				auto &scene_nodes = s["nodes"];
				for (auto node_itr = scene_nodes.Begin(); node_itr != scene_nodes.End(); ++node_itr)
					sc.node_indices.push_back(node_itr->GetUint());
			}

			json_scenes.push_back(move(sc));
		}
	}

	if (doc.HasMember("scene"))
		default_scene_index = doc["scene"].GetUint();
}

static uint32_t padded_type_size(uint32_t type_size)
{
	// If the size if not POT, and not aligned on 32-bit, pad it to be compatible with AMD.
	if ((type_size & 3) && (type_size & (type_size - 1)))
		return (type_size + 3) & ~3;
	else
		return type_size;
}

void Parser::build_primitive(const MeshData::AttributeData &prim)
{
	Mesh mesh;
	mesh.topology = prim.topology;
	mesh.primitive_restart = prim.primitive_restart;
	mesh.has_material = prim.has_material;
	mesh.material_index = prim.material_index;

	auto &positions = prim.attributes[ecast(MeshAttribute::Position)];
	uint32_t vertex_count = json_accessors[positions.accessor_index].count;
	mesh.count = vertex_count;

	vec3 aabb_min(0.0f);
	vec3 aabb_max(0.0f);

	{
		auto &attr = json_accessors[prim.attributes[ecast(MeshAttribute::Position)].accessor_index];
		for (unsigned i = 0; i < std::min(3u, attr.components); i++)
		{
			aabb_min[i] = attr.min[i].f32;
			aabb_max[i] = attr.max[i].f32;
		}
	}

	mesh.static_aabb = AABB(aabb_min, aabb_max);

	bool rebuild_normals = false;
	bool rebuild_tangents = false;

	for (uint32_t i = 0; i < ecast(MeshAttribute::Count); i++)
	{
		if (i == ecast(MeshAttribute::Position) && !prim.attributes[i].active)
			throw logic_error("Mesh must have POSITION semantic.");
		else if (i == ecast(MeshAttribute::Normal) &&
		         !prim.attributes[i].active)
		{
			rebuild_normals = true;
			mesh.attribute_layout[i].format = VK_FORMAT_R32G32B32_SFLOAT;
			mesh.attribute_layout[i].offset = mesh.attribute_stride;
			mesh.attribute_stride += 3 * sizeof(float);
			continue;
		}
		else if (i == ecast(MeshAttribute::Tangent) &&
		         mesh.has_material &&
		         !prim.attributes[i].active &&
		         !materials[mesh.material_index].normal.path.empty())
		{
			rebuild_tangents = true;
			mesh.attribute_layout[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			mesh.attribute_layout[i].offset = mesh.attribute_stride;
			mesh.attribute_stride += 4 * sizeof(float);
			continue;
		}

		if (!prim.attributes[i].active)
		{
			mesh.attribute_layout[i].format = VK_FORMAT_UNDEFINED;
			continue;
		}

		auto &attr = json_accessors[prim.attributes[i].accessor_index];
		if (attr.count != vertex_count)
			throw logic_error("Vertex count mismatch.");

		if (i == ecast(MeshAttribute::BoneIndex))
			mesh.attribute_layout[i].format = VK_FORMAT_R8G8B8A8_UINT;
		else if (i == ecast(MeshAttribute::BoneWeights))
			mesh.attribute_layout[i].format = VK_FORMAT_R16G16B16A16_UNORM;
		else
			mesh.attribute_layout[i].format = components_to_padded_format(attr.type, attr.components);

		if (i == ecast(MeshAttribute::Position))
		{
			mesh.attribute_layout[i].offset = mesh.position_stride;
			mesh.position_stride += padded_type_size(type_stride(attr.type) * attr.components);
		}
		else if (i == ecast(MeshAttribute::BoneIndex))
		{
			mesh.attribute_layout[i].offset = mesh.attribute_stride;
			mesh.attribute_stride += 4;
		}
		else if (i == ecast(MeshAttribute::BoneWeights))
		{
			mesh.attribute_layout[i].offset = mesh.attribute_stride;
			mesh.attribute_stride += 8;
		}
		else
		{
			mesh.attribute_layout[i].offset = mesh.attribute_stride;
			mesh.attribute_stride += padded_type_size(type_stride(attr.type) * attr.components);
		}
	}

	mesh.positions.resize(vertex_count * mesh.position_stride);
	mesh.attributes.resize(vertex_count * mesh.attribute_stride);

	for (uint32_t i = 0; i < ecast(MeshAttribute::Count); i++)
	{
		if (!prim.attributes[i].active)
			continue;

		auto &output = (i == ecast(MeshAttribute::Position)) ? mesh.positions : mesh.attributes;
		auto output_stride = (i == ecast(MeshAttribute::Position)) ? mesh.position_stride : mesh.attribute_stride;

		auto &attr = json_accessors[prim.attributes[i].accessor_index];
		auto &view = json_views[attr.view];
		auto &buffer = json_buffers[view.buffer_index];
		auto type_size = type_stride(attr.type) * attr.components;

		if (i == ecast(MeshAttribute::BoneIndex))
		{
			for (uint32_t v = 0; v < vertex_count; v++)
			{
				uint32_t offset = view.offset + attr.offset + v * attr.stride;
				const auto *data = &buffer[offset];

				uint8_t indices[4] = {};
				if (attr.type == ScalarType::Float32)
				{
					for (uint32_t c = 0; c < attr.components; c++)
						indices[c] = uint8_t(reinterpret_cast<const float *>(data)[c]);
				}
				else if (attr.type == ScalarType::Uint32)
				{
					for (uint32_t c = 0; c < attr.components; c++)
						indices[c] = uint8_t(reinterpret_cast<const uint32_t *>(data)[c]);
				}
				else if (attr.type == ScalarType::Uint16)
				{
					for (uint32_t c = 0; c < attr.components; c++)
						indices[c] = uint8_t(reinterpret_cast<const uint16_t *>(data)[c]);
				}
				else if (attr.type == ScalarType::Uint8)
				{
					for (uint32_t c = 0; c < attr.components; c++)
						indices[c] = data[c];
				}
				else
					throw logic_error("Invalid format for bone indices.");

				memcpy(&output[mesh.attribute_layout[i].offset + output_stride * v], indices, sizeof(indices));
			}
		}
		else if (i == ecast(MeshAttribute::BoneWeights))
		{
			// Need to rescale bone weights. Some meshes don't do this.
			for (uint32_t v = 0; v < vertex_count; v++)
			{
				uint32_t offset = view.offset + attr.offset + v * attr.stride;
				const auto *data = &buffer[offset];

				uint16_t weights[4] = {};
				if (attr.type == ScalarType::Float32)
				{
					float sum = 0.0f;
					for (uint32_t c = 0; c < attr.components; c++)
						sum += reinterpret_cast<const float *>(data)[c];
					float rescale = float(0xffff) / sum;
					for (uint32_t c = 0; c < attr.components; c++)
						weights[c] = uint16_t(reinterpret_cast<const float *>(data)[c] * rescale);
				}
				else if (attr.type == ScalarType::Uint16Unorm)
				{
					float sum = 0.0f;
					for (uint32_t c = 0; c < attr.components; c++)
						sum += reinterpret_cast<const uint16_t *>(data)[c];
					float rescale = float(0xffff) / sum;
					for (uint32_t c = 0; c < attr.components; c++)
						weights[c] = uint16_t(reinterpret_cast<const uint16_t *>(data)[c] * rescale);
				}
				else if (attr.type == ScalarType::Uint8Unorm)
				{
					float sum = 0.0f;
					for (uint32_t c = 0; c < attr.components; c++)
						sum += reinterpret_cast<const uint8_t *>(data)[c];
					float rescale = float(0xffff) / sum;
					for (uint32_t c = 0; c < attr.components; c++)
						weights[c] = uint16_t(reinterpret_cast<const uint8_t *>(data)[c] * rescale);
				}
				else
					throw logic_error("Invalid format for bone weights.");

				memcpy(&output[mesh.attribute_layout[i].offset + output_stride * v], weights, sizeof(weights));
			}
		}
		else
		{
			for (uint32_t v = 0; v < vertex_count; v++)
			{
				uint32_t offset = view.offset + attr.offset + v * attr.stride;
				const auto *data = &buffer[offset];
				memcpy(&output[mesh.attribute_layout[i].offset + output_stride * v], data, type_size);
			}
		}
	}

	if (prim.index_buffer.active)
	{
		auto &indices = json_accessors[prim.index_buffer.accessor_index];
		auto &view = json_views[indices.view];
		auto &buffer = json_buffers[view.buffer_index];

		auto type_size = type_stride(indices.type);
		bool u16_compat = (indices.max[0].u32 < 0xffff) && (indices.max[0].u32 > indices.min[0].u32);
		auto index_count = indices.count;
		auto offset = view.offset + indices.offset;

		if (type_size == 1)
		{
			mesh.indices.resize(sizeof(uint16_t) * index_count);
			mesh.index_type = VK_INDEX_TYPE_UINT16;
			for (uint32_t i = 0; i < index_count; i++)
			{
				const uint8_t *indata = &buffer[indices.stride * i + offset];
				uint16_t *outdata = reinterpret_cast<uint16_t *>(mesh.indices.data()) + i;
				*outdata = uint16_t((*indata == 0xff) ? 0xffff : *indata);
			}
		}
		else if (type_size == 2)
		{
			mesh.indices.resize(sizeof(uint16_t) * index_count);
			mesh.index_type = VK_INDEX_TYPE_UINT16;
			for (uint32_t i = 0; i < index_count; i++)
			{
				const uint16_t *indata = reinterpret_cast<const uint16_t *>(&buffer[indices.stride * i + offset]);
				uint16_t *outdata = reinterpret_cast<uint16_t *>(mesh.indices.data()) + i;
				*outdata = *indata;
			}
		}
		else if (u16_compat)
		{
			mesh.indices.resize(sizeof(uint16_t) * index_count);
			mesh.index_type = VK_INDEX_TYPE_UINT16;
			for (uint32_t i = 0; i < index_count; i++)
			{
				const uint32_t *indata = reinterpret_cast<const uint32_t *>(&buffer[indices.stride * i + offset]);
				uint16_t *outdata = reinterpret_cast<uint16_t *>(mesh.indices.data()) + i;
				*outdata = uint16_t(*indata);
			}
		}
		else
		{
			mesh.indices.resize(sizeof(uint32_t) * index_count);
			mesh.index_type = VK_INDEX_TYPE_UINT32;
			for (uint32_t i = 0; i < index_count; i++)
			{
				const uint32_t *indata = reinterpret_cast<const uint32_t *>(&buffer[indices.stride * i + offset]);
				uint32_t *outdata = reinterpret_cast<uint32_t *>(mesh.indices.data()) + i;
				*outdata = *indata;
			}
		}
		mesh.count = index_count;
	}

	if (rebuild_normals)
		mesh_recompute_normals(mesh);
	if (rebuild_tangents)
		mesh_recompute_tangents(mesh);

	meshes.push_back(move(mesh));
}

void Parser::build_meshes()
{
	mesh_index_to_primitives.resize(json_meshes.size());
	uint32_t primitive_count = 0;
	uint32_t mesh_count = 0;

	for (auto &mesh : json_meshes)
	{
		for (auto &prim : mesh.primitives)
		{
			mesh_index_to_primitives[mesh_count].push_back(primitive_count);
			build_primitive(prim);
			primitive_count++;
		}
		mesh_count++;
	}
}

}
