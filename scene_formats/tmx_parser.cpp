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

#include "tmx_parser.hpp"
#include "rapidjson_wrapper.hpp"
#include "filesystem.hpp"
#include "texture_files.hpp"
#include "texture_utils.hpp"
#include "path.hpp"
#include <stdexcept>

using namespace rapidjson;
using namespace Granite;
using namespace std;

static vector<TMXParser::Property> parse_properties(const Value &properties)
{
	vector<TMXParser::Property> props;
	props.reserve(properties.GetArray().Size());

	for (auto itr = properties.Begin(); itr != properties.End(); ++itr)
	{
		auto &prop = *itr;
		TMXParser::Property p;
		p.name = prop["name"].GetString();

		const char *type = prop["type"].GetString();
		auto &value = prop["value"];
		if (strcmp(type, "bool") == 0)
			p.value.set_boolean(value.GetBool());
		else if (strcmp(type, "int") == 0)
			p.value.set_int(value.GetInt());
		else if (strcmp(type, "float") == 0)
			p.value.set_float(value.GetFloat());
		else if (strcmp(type, "string") == 0)
			p.value.set_string(value.GetString());
		else if (strcmp(type, "file") == 0)
			p.value.set_file(value.GetString());
		else if (strcmp(type, "color") == 0)
		{
			const char *hex = value.GetString();
			if (hex[0] != '#')
				throw logic_error("Invalid color property format.");
			size_t len = strlen(hex);
			uint8_t r = 0, g = 0, b = 0, a = 255;

			if (len == 7)
			{
				auto rgb = strtoul(hex + 1, nullptr, 16);
				r = uint8_t(rgb >> 16);
				g = uint8_t(rgb >> 8);
				b = uint8_t(rgb >> 0);
			}
			else if (len == 9)
			{
				auto rgb = strtoul(hex + 1, nullptr, 16);
				r = uint8_t(rgb >> 16);
				g = uint8_t(rgb >> 8);
				b = uint8_t(rgb >> 0);
				a = uint8_t(rgb >> 24);
			}
			else
				throw logic_error("Invalid format.");

			p.value.set_color(muglm::u8vec4(r, g, b, a));
		}

		props.push_back(move(p));
	}

	return props;
}

TMXParser::TMXParser(const string &path)
{
	string str;
	if (!Global::filesystem()->read_file_to_string(path, str))
		throw runtime_error("Failed to read JSON file.\n");

	parse(path, str);
}

void TMXParser::parse(const string &base_path, const string &json)
{
	Document doc;
	doc.Parse(json);
	if (doc.HasParseError())
		throw runtime_error("Failed to parse JSON.");

	map_size.x = doc["width"].GetUint();
	map_size.y = doc["height"].GetUint();
	tile_size.x = doc["tilewidth"].GetUint();
	tile_size.y = doc["tileheight"].GetUint();

	if (strcmp(doc["orientation"].GetString(), "orthogonal") != 0)
		throw runtime_error("Only orthogonal maps are supported.");
	if (strcmp(doc["renderorder"].GetString(), "right-down") != 0)
		throw runtime_error("Only top-left rendering is supported.");

	layers.resize(doc["layers"].GetArray().Size());

	unsigned layer_index = 0;
	for (auto itr = doc["layers"].Begin(); itr != doc["layers"].End(); ++itr, layer_index++)
	{
		auto &out_layer = layers[layer_index];
		auto &layer = *itr;

		if (layer.HasMember("compression"))
			throw runtime_error("TMX Compression not supported.");

		if (strcmp(layer["type"].GetString(), "tilelayer") != 0)
		{
			out_layer.visible = false;
			continue;
		}

		out_layer.size.x = layer["width"].GetUint();
		out_layer.size.y = layer["height"].GetUint();
		out_layer.visible = layer["visible"].GetBool();
		out_layer.opacity = layer["opacity"].GetFloat();
		out_layer.id = layer["id"].GetUint();

		out_layer.tile_indices.reserve(layer["data"].GetArray().Size());
		for (auto tile_itr = layer["data"].Begin(); tile_itr != layer["data"].End(); ++tile_itr)
			out_layer.tile_indices.push_back(int(tile_itr->GetUint()) - 1);

		if (layer.HasMember("properties"))
			out_layer.properties = parse_properties(layer["properties"]);
	}

	struct Tileset
	{
		unsigned margin;
		unsigned spacing;
		unsigned columns;
		unsigned first_gid;
		unsigned num_tiles;
		int gid_offset;
		string path;
	};
	vector<Tileset> tilesets(doc["tilesets"].GetArray().Size());

	unsigned num_tiles = 0;
	for (auto itr = doc["tilesets"].Begin(); itr != doc["tilesets"].End(); ++itr)
	{
		auto &tileset = *itr;
		num_tiles += tileset["tilecount"].GetUint();
	}
	tiles.resize(num_tiles);

	num_tiles = 0;
	unsigned num_terrains = 0;
	unsigned tileset_index = 0;
	for (auto itr = doc["tilesets"].Begin(); itr != doc["tilesets"].End(); ++itr, tileset_index++)
	{
		auto &tileset = *itr;
		auto &out_tileset = tilesets[tileset_index];

		out_tileset.num_tiles = tileset["tilecount"].GetUint();
		out_tileset.first_gid = tileset["firstgid"].GetUint() - 1;
		out_tileset.gid_offset = int(num_tiles) - int(out_tileset.first_gid);
		out_tileset.margin = tileset["margin"].GetUint();
		out_tileset.spacing = tileset["spacing"].GetUint();
		out_tileset.path = tileset["image"].GetString();
		out_tileset.columns = tileset["columns"].GetUint();

		if (tileset.HasMember("tiles"))
		{
			for (auto tile_itr = tileset["tiles"].Begin(); tile_itr != tileset["tiles"].End(); ++tile_itr)
			{
				auto &tile = *tile_itr;
				auto offset = tile["id"].GetUint();
				if (tile.HasMember("terrain"))
				{
					for (unsigned i = 0; i < 4; i++)
						tiles[num_tiles + offset].terrain_corners[i] = tile["terrain"][i].GetInt();
				}

				if (tile.HasMember("properties"))
					tiles[num_tiles + offset].properties = parse_properties(tile["properties"]);
			}
		}

		num_tiles += out_tileset.num_tiles;

		if (tileset.HasMember("terrains"))
		{
			for (auto terrain_itr = tileset["terrains"].Begin(); terrain_itr != tileset["terrains"].End(); ++terrain_itr)
			{
				auto &terrain = *terrain_itr;
				vector<Property> properties;
				if (terrain.HasMember("properties"))
					properties = parse_properties(terrain["properties"]);
				terrains.push_back({ terrain["name"].GetString(), move(properties) });
			}
			num_terrains += tileset["terrains"].GetArray().Size();
		}
	}

	tilemap.set_2d(VK_FORMAT_R8G8B8A8_SRGB, tile_size.x, tile_size.y, num_tiles, 1);
	if (!tilemap.map_write_scratch())
		throw runtime_error("Failed to map scratch texture.");

	unsigned tile_dst_index = 0;

	for (auto &tileset : tilesets)
	{
		auto file = load_texture_from_file(Path::relpath(base_path, tileset.path), ColorSpace::sRGB);
		if (file.empty())
			throw runtime_error("Failed to load texture.");

		if (file.get_layout().get_format() != VK_FORMAT_R8G8B8A8_SRGB)
			throw runtime_error("Unexpected format.");

		unsigned rows = tileset.num_tiles / tileset.columns;
		for (unsigned y = 0; y < rows; y++)
		{
			for (unsigned x = 0; x < tileset.columns; x++)
			{
				unsigned base_x = tileset.margin;
				unsigned base_y = tileset.margin;
				if (x > 0)
					base_x += (x - 1) * tileset.spacing;
				if (y > 0)
					base_y += (y - 1) * tileset.spacing;

				base_x += x * tile_size.x;
				base_y += y * tile_size.x;

				copy_tile(tilemap.get_layout(), tile_dst_index, file.get_layout(), base_x, base_y);
				auto transparency_type = SceneFormats::image_slice_contains_transparency(tilemap.get_layout(), tile_dst_index, 0);

				auto &pipeline = tiles[tile_dst_index].pipeline;

				switch (transparency_type)
				{
				case SceneFormats::TransparencyType::None:
					pipeline = DrawPipeline::Opaque;
					break;

				case SceneFormats::TransparencyType::Floating:
					pipeline = DrawPipeline::AlphaBlend;
					break;

				case SceneFormats::TransparencyType::Binary:
					pipeline = DrawPipeline::AlphaTest;
					break;
				}

				tile_dst_index++;
			}
		}
	}

	tilemap = SceneFormats::fixup_alpha_edges(tilemap.get_layout(), 0);

	const auto find_tileset = [&](unsigned index) -> const Tileset * {
		for (auto &tileset : tilesets)
			if (index >= tileset.first_gid && index < tileset.first_gid + tileset.num_tiles)
				return &tileset;

		return nullptr;
	};

	// Fixup tilemap indices.
	for (auto &layer : layers)
	{
		for (auto &index : layer.tile_indices)
		{
			if (index < 0)
				continue;

			auto *tileset = find_tileset(index);
			if (tileset)
				index += tileset->gid_offset;
			else
				index = NoTile;
		}
	}
}

void TMXParser::copy_tile(const Vulkan::TextureFormatLayout &dst_layout, unsigned layer,
                          const Vulkan::TextureFormatLayout &src_layout, unsigned base_x, unsigned base_y)
{
	if (base_x + tile_size.x > src_layout.get_width())
		throw runtime_error("Accessing texture out of bounds.");
	if (base_y + tile_size.y > src_layout.get_height())
		throw runtime_error("Accessing texture out of bounds.");

	for (unsigned y = 0; y < tile_size.y; y++)
		for (unsigned x = 0; x < tile_size.x; x++)
			*dst_layout.data_2d<u8vec4>(x, y, layer) = *src_layout.data_2d<u8vec4>(base_x + x, base_y + y);
}

uvec2 TMXParser::get_tile_size() const
{
	return tile_size;
}

uvec2 TMXParser::get_map_tiles() const
{
	return map_size;
}

const vector<TMXParser::Layer> &TMXParser::get_layers() const
{
	return layers;
}

const vector<TMXParser::Terrain> &TMXParser::get_terrains() const
{
	return terrains;
}

const Vulkan::TextureFormatLayout &TMXParser::get_tilemap_image_layout() const
{
	return tilemap.get_layout();
}

const vector<TMXParser::Tile> &TMXParser::get_tiles() const
{
	return tiles;
}

