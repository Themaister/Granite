#include "gltf.hpp"
#include "vulkan.hpp"
#include "filesystem.hpp"
#include "mesh.hpp"
#include <unordered_map>

#define RAPIDJSON_ASSERT(x) do { if (!(x)) throw "JSON error"; } while(0)
#include "rapidjson/document.h"

using namespace std;
using namespace rapidjson;
using namespace Granite;
using namespace Util;

namespace GLTF
{

Parser::Buffer Parser::read_buffer(const string &path, uint64_t length)
{
	auto file = Filesystem::get().open(path);
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

Parser::Parser(const std::string &path)
{
	auto file = Filesystem::get().open(path);
	if (!file)
		throw runtime_error("Failed to load GLTF file.");

	auto length = file->get_size();
	auto *buffer = static_cast<const char *>(file->map());
	if (!buffer)
		throw runtime_error("Failed to map GLTF file.");

	string json(buffer, buffer + length);
	parse(path, json);
}

#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406

VkFormat Parser::components_to_format(ScalarType type, uint32_t components)
{
	switch (type)
	{
	case ScalarType::Int8:
	{
		static const VkFormat formats[] = { VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8A8_SINT };
		return formats[components];
	}
	case ScalarType::Int8Snorm:
	{
		static const VkFormat formats[] = { VK_FORMAT_R8_SNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8A8_SNORM };
		return formats[components];
	}
	case ScalarType::Uint8:
	{
		static const VkFormat formats[] = { VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT };
		return formats[components];
	}
	case ScalarType::Uint8Unorm:
	{
		static const VkFormat formats[] = { VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
		return formats[components];
	}
	case ScalarType::Int16:
	{
		static const VkFormat formats[] = { VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT };
		return formats[components];
	}
	case ScalarType::Int16Snorm:
	{
		static const VkFormat formats[] = { VK_FORMAT_R16_SNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16B16_SNORM, VK_FORMAT_R16G16B16A16_SNORM };
		return formats[components];
	}
	case ScalarType::Uint16:
	{
		static const VkFormat formats[] = { VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT };
		return formats[components];
	}
	case ScalarType::Uint16Unorm:
	{
		static const VkFormat formats[] = { VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16B16_UNORM, VK_FORMAT_R16G16B16A16_UNORM };
		return formats[components];
	}
	case ScalarType::Int32:
	{
		static const VkFormat formats[] = { VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT };
		return formats[components];
	}
	case ScalarType::Uint32:
	{
		static const VkFormat formats[] = { VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT };
		return formats[components];
	}
	case ScalarType::Float32:
	{
		static const VkFormat formats[] = { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };
		return formats[components];
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
	case ScalarType::Uint16:
	case ScalarType::Int16Snorm:
	case ScalarType::Uint16Unorm:
		return 2;

	case ScalarType::Int32:
	case ScalarType::Uint32:
	case ScalarType::Float32:
		return 4;

	default:
		return 0;
	}
}

void Parser::resolve_component_type(uint32_t component_type, const char *type, bool normalized,
                                    VkFormat &format, ScalarType &scalar_type, uint32_t &components, uint32_t &stride)
{
	if (!strcmp(type, "SCALAR"))
		components = 1;
	else if (!strcmp(type, "VEC2"))
		components = 2;
	else if (!strcmp(type, "VEC3"))
		components = 3;
	else if (!strcmp(type, "VEC4"))
		components = 4;
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
	}

	stride = components * type_stride(scalar_type);
	format = components_to_format(scalar_type, components);
}

static uint32_t get_by_name(const unordered_map<string, uint32_t> &map, const string &v)
{
	auto itr = map.find(v);
	if (itr == end(map))
		throw runtime_error("Accessor does not exist.");
	return itr->second;
}

template <typename T>
static void iterate_elements(const Value &value, const T &t, unordered_map<string, uint32_t> &map)
{
	if (value.IsArray())
	{
		for (auto itr = value.Begin(); itr != value.End(); ++itr)
		{
			t(*itr);
		}
	}
	else
	{
		for (auto itr = value.MemberBegin(); itr != value.MemberEnd(); ++itr)
		{
			auto size = map.size();
			map[itr->name.GetString()] = size;
			t(itr->value);
		}
	}
}

template <typename T>
static void read_min_max(T &out, ScalarType type, const Value &v)
{
	switch (type)
	{
	case ScalarType::Float32:
		out.f32 = v.GetFloat();
		break;

	case ScalarType::Int8:
	case ScalarType::Int16:
	case ScalarType::Int32:
		out.i32 = v.GetInt();
		break;

	case ScalarType::Uint8:
	case ScalarType::Uint16:
	case ScalarType::Uint32:
		out.i32 = v.GetInt();
		break;

	case ScalarType::Int8Snorm:
		out.f32 = clamp(float(v.GetInt()) / 0x7f, -1.0f, 1.0f);
		break;
	case ScalarType::Int16Snorm:
		out.f32 = clamp(float(v.GetInt()) / 0x7fff, -1.0f, 1.0f);
		break;
	case ScalarType::Uint8Unorm:
		out.f32 = clamp(float(v.GetUint()) / 0xff, 0.0f, 1.0f);
		break;
	case ScalarType::Uint16Unorm:
		out.f32 = clamp(float(v.GetUint()) / 0xffff, 0.0f, 1.0f);
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

void Parser::parse(const string &original_path, const string &json)
{
	Document doc;
	doc.Parse(json);

	const auto add_buffer = [&](const Value &buf) {
		const char *uri = buf["uri"].GetString();
		auto length = buf["byteLength"].GetInt64();
		auto path = Path::relpath(original_path, uri);

		json_buffers.push_back(read_buffer(path, length));
	};

	const auto add_view = [&](const Value &view) {
		auto &buf = view["buffer"];
		auto buffer_index = buf.IsString() ? get_by_name(json_buffer_map, buf.GetString()) : buf.GetUint();
		auto offset = view["byteOffset"].GetUint();
		auto length = view["byteLength"].GetUint();
		auto target = view["target"].GetUint();

		json_views.push_back({buffer_index, offset, length, target});
	};

	const auto add_accessor = [&](const Value &accessor) {
		auto &view = accessor["bufferView"];
		auto view_index = view.IsString() ? get_by_name(json_view_map, view.GetString()) : view.GetUint();

		auto offset = accessor["byteOffset"].GetUint();
		auto component_type = accessor["componentType"].GetUint();
		auto count = accessor["count"].GetUint();
		auto *type = accessor["type"].GetString();
		bool normalized = false;
		if (accessor.HasMember("normalized"))
			normalized = accessor["normalized"].GetBool();

		Accessor acc = {};
		resolve_component_type(component_type, type, normalized, acc.format, acc.type, acc.components, acc.stride);
		acc.view = view_index;
		acc.offset = offset;
		acc.count = count;

		if (accessor.HasMember("byteStride"))
			acc.stride = accessor["byteStride"].GetUint();

		auto *minimums = acc.min;
		for (auto itr = accessor["min"].Begin(); itr != accessor["min"].End(); ++itr)
		{
			assert(minimums - acc.min < 16);
			read_min_max(*minimums, acc.type, *itr);
			minimums++;
		}

		auto *maximums = acc.max;
		for (auto itr = accessor["max"].Begin(); itr != accessor["max"].End(); ++itr)
		{
			assert(maximums - acc.max < 16);
			read_min_max(*maximums, acc.type, *itr);
			maximums++;
		}

		json_accessors.push_back(acc);
	};

	const auto parse_primitive = [&](const Value &primitive) -> MeshData::AttributeData {
		MeshData::AttributeData attr = {};
		if (primitive.HasMember("indices"))
		{
			attr.index_buffer.active = true;
			auto &indices = primitive["indices"];
			attr.index_buffer.accessor_index = indices.IsString() ? get_by_name(json_accessor_map, indices.GetString()) : indices.GetUint();
		}

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

		auto &attrs = primitive["attributes"];
		for (auto itr = attrs.MemberBegin(); itr != attrs.MemberEnd(); ++itr)
		{
			auto *semantic = itr->name.GetString();
			uint32_t accessor_index = itr->value.IsString() ? get_by_name(json_accessor_map, itr->value.GetString()) : itr->value.GetUint();
			MeshAttribute attribute = semantic_to_attribute(semantic);

			attr.attributes[ecast(attribute)].accessor_index = accessor_index;
			attr.attributes[ecast(attribute)].active = true;
		}

		return attr;
	};

	const auto add_mesh = [&](const Value &mesh) {
		auto &prims = mesh["primitives"];
		for (auto itr = prims.Begin(); itr != prims.End(); ++itr)
		{
			MeshData data;
			data.primitives.push_back(parse_primitive(*itr));
			json_meshes.push_back(data);
		}
	};

	iterate_elements(doc["buffers"], add_buffer, json_buffer_map);
	iterate_elements(doc["bufferViews"], add_view, json_view_map);
	iterate_elements(doc["accessors"], add_accessor, json_accessor_map);
	iterate_elements(doc["meshes"], add_mesh, json_mesh_map);
}

}
