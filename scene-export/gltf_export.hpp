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

#include "scene_formats.hpp"
#include "texture_compression.hpp"

namespace Granite
{
namespace SceneFormats
{
enum class TextureCompression
{
	BC7,
	BC3,
	BC4,
	BC5,
	BC1,
	BC6H,
	ASTC4x4,
	ASTC5x5,
	ASTC6x6,
	ASTC8x8,
	PNG,
	Uncompressed
};

enum class TextureCompressionFamily
{
	BC,
	ASTC,
	PNG,
	Uncompressed
};

struct ExportOptions
{
	TextureCompressionFamily compression = TextureCompressionFamily::Uncompressed;
	unsigned texcomp_quality = 3;
	unsigned threads = 0;

	struct
	{
		std::string cube;
		std::string reflection;
		std::string irradiance;
		vec3 fog_color = vec3(0.0f);
		float fog_falloff = 0.0f;

		TextureCompressionFamily compression = TextureCompressionFamily::Uncompressed;
		unsigned texcomp_quality = 3;
		float intensity = 1.0f;
	} environment;

	bool quantize_attributes = false;
	bool optimize_meshes = false;
	bool stripify_meshes = false;
	bool gltf = false;
};

bool export_scene_to_glb(const SceneInformation &scene, const std::string &path, const ExportOptions &options);
}
}