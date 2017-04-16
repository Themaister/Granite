#include "gltf.hpp"
#include "vulkan.hpp"
#include "filesystem.hpp"
#include <unordered_map>

#define RAPIDJSON_ASSERT(x) do { if (!(x)) throw "JSON error"; } while(0)
#include "rapidjson/document.h"

using namespace std;
using namespace rapidjson;
using namespace Granite;

namespace GLTF
{
using Buffer = std::vector<uint8_t>;

struct BufferView
{
	uint32_t buffer_index;
	uint32_t offset;
	uint32_t length;
	uint32_t target;
};

enum class ScalarType
{
	Float32,
	Int32,
	Uint32,
	Int16,
	Uint16,
	Int8,
	Uint8,
	Int16Snorm,
	Uint16Unorm,
	Int8Snorm,
	Uint8Unorm
};

struct Accessor
{
	uint32_t view;
	uint32_t offset;
	uint32_t count;
	uint32_t stride;

	VkFormat format;
	ScalarType type;
	uint32_t components;

	union
	{
		float f32;
		uint32_t u32;
	} min[16], max[16];
};

static Buffer read_buffer(const string &path, uint64_t length)
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

static VkFormat components_to_format(ScalarType type, uint32_t components)
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

static uint32_t type_stride(ScalarType type)
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

static void resolve_component_type(uint32_t component_type, const char *type, bool normalized,
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

void Parser::parse(const string &original_path, const string &json)
{
	Document doc;
	doc.Parse(json);

	vector<Buffer> json_buffers;
	unordered_map<string, uint32_t> json_buffer_map;
	const auto get_buffer_index_by_name = [&](const string &v) -> uint32_t {
		auto itr = json_buffer_map.find(v);
		if (itr == end(json_buffer_map))
			throw runtime_error("Buffer does not exist.");
		return itr->second;
	};

	const auto add_buffer = [&](const Value &buf) {
		const char *uri = buf["uri"].GetString();
		auto length = buf["byteLength"].GetInt64();
		auto path = Path::relpath(original_path, uri);
		json_buffers.push_back(read_buffer(path, length));
	};

	vector<BufferView> json_views;
	unordered_map<string, uint32_t> json_view_map;
	const auto add_view = [&](const Value &view) {
		auto &buf = view["buffer"];
		auto buffer_index = buf.IsString() ? get_buffer_index_by_name(buf.GetString()) : buf.GetUint();
		auto offset = view["byteOffset"].GetUint();
		auto length = view["byteLength"].GetUint();
		auto target = view["target"].GetUint();
		json_views.push_back({buffer_index, offset, length, target});
	};

	const auto get_buffer_view_by_name = [&](const string &v) -> uint32_t {
		auto itr = json_view_map.find(v);
		if (itr == end(json_view_map))
			throw runtime_error("BufferView does not exist.");
		return itr->second;
	};

	vector<Accessor> json_accessors;
	unordered_map<string, uint32_t> json_accessor_map;
	const auto add_accessor = [&](const Value &accessor) {
		auto &view = accessor["bufferView"];
		auto view_index = view.IsString() ? get_buffer_view_by_name(view.GetString()) : view.GetUint();

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
			minimums->f32 = itr->GetFloat();
			minimums++;
		}

		auto *maximums = acc.max;
		for (auto itr = accessor["max"].Begin(); itr != accessor["max"].End(); ++itr)
		{
			assert(maximums - acc.max < 16);
			maximums->f32 = itr->GetFloat();
			maximums++;
		}
		json_accessors.push_back(acc);
	};

	const auto get_accessor_by_name = [&](const string &v) -> uint32_t {
		auto itr = json_accessor_map.find(v);
		if (itr == end(json_accessor_map))
			throw runtime_error("Accessor does not exist.");
		return itr->second;
	};

	auto &buffers = doc["buffers"];
	if (buffers.IsArray())
	{
		for (auto itr = buffers.Begin(); itr != buffers.End(); ++itr)
		{
			auto &buf = *itr;
			add_buffer(buf);
			const char *uri = buf["uri"].GetString();
			auto length = buf["byteLength"].GetInt64();
			auto path = Path::relpath(original_path, uri);
			json_buffers.push_back(read_buffer(path, length));
		}
	}
	else
	{
		for (auto itr = buffers.MemberBegin(); itr != buffers.MemberEnd(); ++itr)
		{
			auto &buf = itr->value;
			json_buffer_map[itr->name.GetString()] = json_buffers.size();
			add_buffer(buf);
		}
	}

	auto &views = doc["bufferViews"];
	if (views.IsArray())
	{
		for (auto itr = views.Begin(); itr != views.End(); ++itr)
		{
			auto &view = *itr;
			add_view(view);
		}
	}
	else
	{
		for (auto itr = views.MemberBegin(); itr != views.MemberEnd(); ++itr)
		{
			auto &view = itr->value;
			json_view_map[itr->name.GetString()] = json_views.size();
			add_view(view);
		}
	}

	auto &accessors = doc["accessors"];
	if (accessors.IsArray())
	{
		for (auto itr = accessors.Begin(); itr != accessors.End(); ++itr)
		{
			auto &accessor = *itr;
			add_accessor(accessor);
		}
	}
	else
	{
		for (auto itr = accessors.MemberBegin(); itr != accessors.MemberEnd(); ++itr)
		{
			auto &accessor = itr->value;
			json_accessor_map[itr->name.GetString()] = json_accessors.size();
			add_accessor(accessor);
		}
	}

}

}
