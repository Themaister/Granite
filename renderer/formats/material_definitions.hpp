/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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
#include "math.hpp"
#include "enum_cast.hpp"

namespace Granite
{
enum class DrawPipeline : unsigned char
{
	Opaque,
	AlphaTest,
	AlphaBlend,
	Count
};

enum class DrawPipelineCoverage : unsigned char
{
	Full,
	Modifies
};

enum class TextureKind : unsigned
{
	BaseColor = 0,
	Normal = 1,
	MetallicRoughness = 2,
	Occlusion = 3,
	Emissive = 4,
	Count
};

enum class SamplerFamily
{
	Wrap,
	Clamp
};

struct MaterialInfo
{
	std::string paths[Util::ecast(TextureKind::Count)];
	vec4 uniform_base_color = vec4(1.0f);
	vec3 uniform_emissive_color = vec3(0.0f);
	float uniform_metallic = 1.0f;
	float uniform_roughness = 1.0f;
	float normal_scale = 1.0f;
	DrawPipeline pipeline = DrawPipeline::Opaque;
	SamplerFamily sampler = SamplerFamily::Wrap;
	uint32_t shader_variant = 0;
	bool two_sided = false;
};

enum MaterialTextureFlagBits
{
	MATERIAL_TEXTURE_BASE_COLOR_BIT = 1u << Util::ecast(TextureKind::BaseColor),
	MATERIAL_TEXTURE_NORMAL_BIT = 1u << Util::ecast(TextureKind::Normal),
	MATERIAL_TEXTURE_METALLIC_ROUGHNESS_BIT = 1u << Util::ecast(TextureKind::MetallicRoughness),
	MATERIAL_TEXTURE_OCCLUSION_BIT = 1u << Util::ecast(TextureKind::Occlusion),
	MATERIAL_TEXTURE_EMISSIVE_BIT = 1u << Util::ecast(TextureKind::Emissive),
	MATERIAL_EMISSIVE_BIT = 1u << 5,
};

enum MaterialShaderVariantFlagBits
{
	MATERIAL_SHADER_VARIANT_NONE
};

}