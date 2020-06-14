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

#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include "memory_mapped_texture.hpp"
#include "abstract_renderable.hpp"
#include "math.hpp"

class TMXParser
{
public:
	explicit TMXParser(const std::string &path);
	enum { NoTile = -1 };

	class Value
	{
	public:
		enum class Type
		{
			Float,
			Int,
			Color,
			File,
			String,
			Boolean,
			None
		};

		void set_float(float v) { u.f32 = v; type = Type::Float; }
		void set_int(int v) { u.s32 = v; type = Type::Int; }
		void set_color(const muglm::u8vec4 &v) { for (int i = 0; i < 4; i++) u.color[i] = v[i]; type = Type::Color; }
		void set_file(const char *v) { str = v; type = Type::File; }
		void set_string(const char *v) { str = v; type = Type::String; }
		void set_boolean(bool v) { u.boolean = v; type = Type::Boolean; }

		float get_float() const { verify(Type::Float); return u.f32; }
		int get_int() const { verify(Type::Int); return u.s32; }
		muglm::u8vec4 get_color() const { verify(Type::Color); return muglm::u8vec4(u.color[0], u.color[1], u.color[2], u.color[3]); }
		const std::string &get_file() const { verify(Type::File); return str; }
		const std::string &get_string() const { verify(Type::String); return str; }
		bool get_boolean() const { verify(Type::Boolean); return u.boolean; }

	private:
		void verify(Type expected) const
		{
			if (expected != type)
				throw std::logic_error("Type mismatch!");
		}

		union
		{
			float f32;
			int32_t s32;
			uint8_t color[4];
			bool boolean;
		} u;
		std::string str;
		Type type = Type::None;
	};

	struct Property
	{
		std::string name;
		Value value;
	};

	struct Tile
	{
		// Tile properties go here.
		Granite::DrawPipeline pipeline = Granite::DrawPipeline::Opaque;
		int terrain_corners[4] = { -1, -1, -1, -1 };
		std::vector<Property> properties;
	};

	struct Terrain
	{
		std::string name;
		std::vector<Property> properties;
	};

	struct Layer
	{
		std::vector<int> tile_indices;
		std::vector<Property> properties;
		muglm::ivec2 offset;
		muglm::uvec2 size;
		unsigned id;
		float opacity;
		bool visible;
	};

	const std::vector<Tile> &get_tiles() const;
	const std::vector<Layer> &get_layers() const;
	const std::vector<Terrain> &get_terrains() const;
	const Vulkan::TextureFormatLayout &get_tilemap_image_layout() const;

	muglm::uvec2 get_tile_size() const;
	muglm::uvec2 get_map_tiles() const;

private:
	Granite::SceneFormats::MemoryMappedTexture tilemap;
	std::vector<Tile> tiles;
	std::vector<Layer> layers;
	std::vector<Terrain> terrains;
	muglm::uvec2 map_size;
	muglm::uvec2 tile_size;

	void parse(const std::string &base_path, const std::string &json);
	void copy_tile(const Vulkan::TextureFormatLayout &dst_layout, unsigned layer,
	               const Vulkan::TextureFormatLayout &src_layout, unsigned x, unsigned y);
};
