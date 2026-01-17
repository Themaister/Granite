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

#include "enum_cast.hpp"
#include "volk.h"

namespace Granite
{
enum class MeshAttribute : unsigned
{
	Position = 0,
	UV = 1,
	Normal = 2,
	Tangent = 3,
	BoneIndex = 4,
	BoneWeights = 5,
	VertexColor = 6,
	Count,
	None
};

enum MeshAttributeFlagBits
{
	MESH_ATTRIBUTE_POSITION_BIT = 1u << Util::ecast(MeshAttribute::Position),
	MESH_ATTRIBUTE_UV_BIT = 1u << Util::ecast(MeshAttribute::UV),
	MESH_ATTRIBUTE_NORMAL_BIT = 1u << Util::ecast(MeshAttribute::Normal),
	MESH_ATTRIBUTE_TANGENT_BIT = 1u << Util::ecast(MeshAttribute::Tangent),
	MESH_ATTRIBUTE_BONE_INDEX_BIT = 1u << Util::ecast(MeshAttribute::BoneIndex),
	MESH_ATTRIBUTE_BONE_WEIGHTS_BIT = 1u << Util::ecast(MeshAttribute::BoneWeights),
	MESH_ATTRIBUTE_VERTEX_COLOR_BIT = 1u << Util::ecast(MeshAttribute::VertexColor)
};

struct MeshAttributeLayout
{
	VkFormat format = VK_FORMAT_UNDEFINED;
	uint32_t offset = 0;
};

}