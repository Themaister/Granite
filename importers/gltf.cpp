#include "gltf.hpp"
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

}
}
