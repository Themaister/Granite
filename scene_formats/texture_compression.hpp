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

#include "material.hpp"
#include "thread_group.hpp"
#include "memory_mapped_texture.hpp"

namespace Granite
{
enum class TextureMode
{
	RGB,
	RGBA,
	sRGB,
	sRGBA,
	Luminance,
	Normal,
	Mask,
	NormalLA, // Special encoding to help certain formats where we encode as LLL + A.
	MaskLA, // Special encoding to help certain formats where we encode as LLL + A.
	HDR,
	Unknown
};

struct CompressorArguments
{
	std::string output;
	VkFormat format = VK_FORMAT_UNDEFINED;
	unsigned quality = 3;
	TextureMode mode = TextureMode::Unknown;
	VkComponentMapping output_mapping = {
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_B,
		VK_COMPONENT_SWIZZLE_A,
	};
	bool deferred_mipgen = false;
};

VkFormat string_to_format(const std::string &s);
bool compress_texture(ThreadGroup &group, const CompressorArguments &args,
                      const std::shared_ptr<SceneFormats::MemoryMappedTexture> &input,
                      TaskGroupHandle &dep, TaskSignal *signal);
}