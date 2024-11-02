// Modifications Copyright (c) 2021. Advanced Micro Devices, Inc. All Rights Reserved.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016, Intel Corporation
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File changes (yyyy-mm-dd)
// 2016-09-07: filip.strugar@intel.com: first commit
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Granite implementation:
// Copyright (c) 2022-2024 Hans-Kristian Arntzen

#include "ffx_cacao_impl.h"
#include "ffx_cacao_defines.h"

#include <assert.h>
#include <math.h>   // cos, sin
#include <string.h> // memcpy
#include <stdio.h>  // snprintf

// Define symbol to enable DirectX debug markers created using Cauldron
#define FFX_CACAO_ENABLE_CAULDRON_DEBUG

#define FFX_CACAO_ASSERT(exp) assert(exp)
#define FFX_CACAO_ARRAY_SIZE(xs) (sizeof(xs)/sizeof(xs[0]))
#define FFX_CACAO_COS(x) cosf(x)
#define FFX_CACAO_SIN(x) sinf(x)
#define FFX_CACAO_MIN(x, y) (((x) < (y)) ? (x) : (y))
#define FFX_CACAO_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define FFX_CACAO_CLAMP(value, lower, upper) FFX_CACAO_MIN(FFX_CACAO_MAX(value, lower), upper)
#define FFX_CACAO_OFFSET_OF(T, member) (size_t)(&(((T*)0)->member))

#define MAX_BLUR_PASSES 8

inline static uint32_t dispatchSize(uint32_t tileSize, uint32_t totalSize)
{
	return (totalSize + tileSize - 1) / tileSize;
}

// TIMESTAMP_FORMAT(name, vulkan_format, d3d12_format)
#define TEXTURE_FORMATS \
	TEXTURE_FORMAT(R16_SFLOAT,          VK_FORMAT_R16_SFLOAT,          DXGI_FORMAT_R16_FLOAT) \
	TEXTURE_FORMAT(R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT) \
	TEXTURE_FORMAT(R8G8B8A8_SNORM,      VK_FORMAT_R8G8B8A8_SNORM,      DXGI_FORMAT_R8G8B8A8_SNORM) \
	TEXTURE_FORMAT(R8G8_UNORM,          VK_FORMAT_R8G8_UNORM,          DXGI_FORMAT_R8G8_UNORM) \
	TEXTURE_FORMAT(R8_UNORM,            VK_FORMAT_R8_UNORM,            DXGI_FORMAT_R8_UNORM)

typedef enum TextureFormatID {
#define TEXTURE_FORMAT(name, _vulkan_format, _d3d12_format) TEXTURE_FORMAT_##name,
	TEXTURE_FORMATS
#undef TEXTURE_FORMAT
} TextureFormatID;

static const VkFormat TEXTURE_FORMAT_LOOKUP_VK[] = {
#define TEXTURE_FORMAT(_name, vulkan_format, _d3d12_format) vulkan_format,
	TEXTURE_FORMATS
#undef TEXTURE_FORMAT
};

// TEXTURE(name, width, height, texture_format, array_size, num_mips)
#define TEXTURES \
	TEXTURE(DEINTERLEAVED_DEPTHS,    deinterleavedDepthBufferWidth, deinterleavedDepthBufferHeight, TEXTURE_FORMAT_R16_SFLOAT,          4, 4) \
	TEXTURE(DEINTERLEAVED_NORMALS,   ssaoBufferWidth,               ssaoBufferHeight,               TEXTURE_FORMAT_R8G8B8A8_SNORM,      4, 1) \
	TEXTURE(SSAO_BUFFER_PING,        ssaoBufferWidth,               ssaoBufferHeight,               TEXTURE_FORMAT_R8G8_UNORM,          4, 1) \
	TEXTURE(SSAO_BUFFER_PONG,        ssaoBufferWidth,               ssaoBufferHeight,               TEXTURE_FORMAT_R8G8_UNORM,          4, 1) \
	TEXTURE(IMPORTANCE_MAP,          importanceMapWidth,            importanceMapHeight,            TEXTURE_FORMAT_R8_UNORM,            1, 1) \
	TEXTURE(IMPORTANCE_MAP_PONG,     importanceMapWidth,            importanceMapHeight,            TEXTURE_FORMAT_R8_UNORM,            1, 1) \
	TEXTURE(DOWNSAMPLED_SSAO_BUFFER, downsampledSsaoBufferWidth,    downsampledSsaoBufferHeight,    TEXTURE_FORMAT_R8_UNORM,            1, 1)

typedef enum TextureID {
#define TEXTURE(name, _width, _height, _format, _array_size, _num_mips) TEXTURE_##name,
	TEXTURES
#undef TEXTURE
	NUM_TEXTURES
} TextureID;

typedef struct TextureMetaData {
	size_t widthOffset;
	size_t heightOffset;
	TextureFormatID format;
	uint32_t arraySize;
	uint32_t numMips;
	const char *name;
} TextureMetaData;

static const TextureMetaData TEXTURE_META_DATA[NUM_TEXTURES] = {
#define TEXTURE(name, width, height, format, array_size, num_mips) { FFX_CACAO_OFFSET_OF(FFX_CACAO_BufferSizeInfo, width), FFX_CACAO_OFFSET_OF(FFX_CACAO_BufferSizeInfo, height), format, array_size, num_mips, "FFX_CACAO_" #name },
	TEXTURES
#undef TEXTURE
};

// DESCRIPTOR_SET_LAYOUT(name, num_inputs, num_outputs)
#define DESCRIPTOR_SET_LAYOUTS \
	DESCRIPTOR_SET_LAYOUT(CLEAR_LOAD_COUNTER,                 0, 1) \
	DESCRIPTOR_SET_LAYOUT(PREPARE_DEPTHS,                     1, 1) \
	DESCRIPTOR_SET_LAYOUT(PREPARE_DEPTHS_MIPS,                1, 4) \
	DESCRIPTOR_SET_LAYOUT(PREPARE_POINTS,                     1, 1) \
	DESCRIPTOR_SET_LAYOUT(PREPARE_POINTS_MIPS,                1, 4) \
	DESCRIPTOR_SET_LAYOUT(PREPARE_NORMALS,                    1, 1) \
	DESCRIPTOR_SET_LAYOUT(PREPARE_NORMALS_FROM_INPUT_NORMALS, 1, 1) \
	DESCRIPTOR_SET_LAYOUT(GENERATE,                           2, 1) \
	DESCRIPTOR_SET_LAYOUT(GENERATE_ADAPTIVE,                  5, 1) \
	DESCRIPTOR_SET_LAYOUT(GENERATE_IMPORTANCE_MAP,            1, 1) \
	DESCRIPTOR_SET_LAYOUT(POSTPROCESS_IMPORTANCE_MAP_A,       1, 1) \
	DESCRIPTOR_SET_LAYOUT(POSTPROCESS_IMPORTANCE_MAP_B,       1, 2) \
	DESCRIPTOR_SET_LAYOUT(EDGE_SENSITIVE_BLUR,                1, 1) \
	DESCRIPTOR_SET_LAYOUT(APPLY,                              1, 1) \
	DESCRIPTOR_SET_LAYOUT(BILATERAL_UPSAMPLE,                 4, 1)

typedef enum DescriptorSetLayoutID {
#define DESCRIPTOR_SET_LAYOUT(name, _num_inputs, _num_outputs) DSL_##name,
	DESCRIPTOR_SET_LAYOUTS
#undef DESCRIPTOR_SET_LAYOUT
	NUM_DESCRIPTOR_SET_LAYOUTS
} DescriptorSetLayoutID;

typedef struct DescriptorSetLayoutMetaData {
	uint32_t    numInputs;
	uint32_t    numOutputs;
	const char *name;
} DescriptorSetLayoutMetaData;

// DESCRIPTOR_SET(name, layout_name, pass)
#define DESCRIPTOR_SETS \
	DESCRIPTOR_SET(CLEAR_LOAD_COUNTER,                 CLEAR_LOAD_COUNTER,                 0) \
	DESCRIPTOR_SET(PREPARE_DEPTHS,                     PREPARE_DEPTHS,                     0) \
	DESCRIPTOR_SET(PREPARE_DEPTHS_MIPS,                PREPARE_DEPTHS_MIPS,                0) \
	DESCRIPTOR_SET(PREPARE_POINTS,                     PREPARE_POINTS,                     0) \
	DESCRIPTOR_SET(PREPARE_POINTS_MIPS,                PREPARE_POINTS_MIPS,                0) \
	DESCRIPTOR_SET(PREPARE_NORMALS,                    PREPARE_NORMALS,                    0) \
	DESCRIPTOR_SET(PREPARE_NORMALS_FROM_INPUT_NORMALS, PREPARE_NORMALS_FROM_INPUT_NORMALS, 0) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_BASE_0,           GENERATE,                           0) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_BASE_1,           GENERATE,                           1) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_BASE_2,           GENERATE,                           2) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_BASE_3,           GENERATE,                           3) \
	DESCRIPTOR_SET(GENERATE_0,                         GENERATE,                           0) \
	DESCRIPTOR_SET(GENERATE_1,                         GENERATE,                           1) \
	DESCRIPTOR_SET(GENERATE_2,                         GENERATE,                           2) \
	DESCRIPTOR_SET(GENERATE_3,                         GENERATE,                           3) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_0,                GENERATE_ADAPTIVE,                  0) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_1,                GENERATE_ADAPTIVE,                  1) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_2,                GENERATE_ADAPTIVE,                  2) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_3,                GENERATE_ADAPTIVE,                  3) \
	DESCRIPTOR_SET(GENERATE_IMPORTANCE_MAP,            GENERATE_IMPORTANCE_MAP,            0) \
	DESCRIPTOR_SET(POSTPROCESS_IMPORTANCE_MAP_A,       POSTPROCESS_IMPORTANCE_MAP_A,       0) \
	DESCRIPTOR_SET(POSTPROCESS_IMPORTANCE_MAP_B,       POSTPROCESS_IMPORTANCE_MAP_B,       0) \
	DESCRIPTOR_SET(EDGE_SENSITIVE_BLUR_0,              EDGE_SENSITIVE_BLUR,                0) \
	DESCRIPTOR_SET(EDGE_SENSITIVE_BLUR_1,              EDGE_SENSITIVE_BLUR,                1) \
	DESCRIPTOR_SET(EDGE_SENSITIVE_BLUR_2,              EDGE_SENSITIVE_BLUR,                2) \
	DESCRIPTOR_SET(EDGE_SENSITIVE_BLUR_3,              EDGE_SENSITIVE_BLUR,                3) \
	DESCRIPTOR_SET(APPLY_PING,                         APPLY,                              0) \
	DESCRIPTOR_SET(APPLY_PONG,                         APPLY,                              0) \
	DESCRIPTOR_SET(BILATERAL_UPSAMPLE_PING,            BILATERAL_UPSAMPLE,                 0) \
	DESCRIPTOR_SET(BILATERAL_UPSAMPLE_PONG,            BILATERAL_UPSAMPLE,                 0)

typedef enum DescriptorSetID {
#define DESCRIPTOR_SET(name, _layout_name, _pass) DS_##name,
	DESCRIPTOR_SETS
#undef DESCRIPTOR_SET
	NUM_DESCRIPTOR_SETS
} DescriptorSetID;

typedef struct DescriptorSetMetaData {
	DescriptorSetLayoutID descriptorSetLayoutID;
	uint32_t pass;
	const char *name;
} DescriptorSetMetaData;

static const DescriptorSetMetaData DESCRIPTOR_SET_META_DATA[NUM_DESCRIPTOR_SETS] = {
#define DESCRIPTOR_SET(name, layout_name, pass) { DSL_##layout_name, pass, "FFX_CACAO_DS_" #name },
	DESCRIPTOR_SETS
#undef DESCRIPTOR_SET
};

// VIEW_TYPE(name, vulkan_view_type, d3d12_view_type_srv)
#define VIEW_TYPES \
	VIEW_TYPE(2D,       VK_IMAGE_VIEW_TYPE_2D,       D3D12_SRV_DIMENSION_TEXTURE2D,      D3D12_UAV_DIMENSION_TEXTURE2D) \
	VIEW_TYPE(2D_ARRAY, VK_IMAGE_VIEW_TYPE_2D_ARRAY, D3D12_SRV_DIMENSION_TEXTURE2DARRAY, D3D12_UAV_DIMENSION_TEXTURE2DARRAY)

typedef enum ViewTypeID {
#define VIEW_TYPE(name, _vulkan_view_type, _d3d12_view_type_srv, _d3d12_view_type_uav) VIEW_TYPE_##name,
	VIEW_TYPES
#undef VIEW_TYPE
} ViewTypeID;

static const VkImageViewType VIEW_TYPE_LOOKUP_VK[] = {
#define VIEW_TYPE(_name, vulkan_view_type, _d3d12_view_type_srv, _d3d12_view_type_uav) vulkan_view_type,
	VIEW_TYPES
#undef VIEW_TYPE
};

// SHADER_RESOURCE_VIEW(name, texture, view_dimension, most_detailed_mip, mip_levels, first_array_slice, array_size)
#define SHADER_RESOURCE_VIEWS \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_DEPTHS,    DEINTERLEAVED_DEPTHS,  VIEW_TYPE_2D_ARRAY, 0, 4, 0, 4) \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_DEPTHS_0,  DEINTERLEAVED_DEPTHS,  VIEW_TYPE_2D_ARRAY, 0, 4, 0, 1) \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_DEPTHS_1,  DEINTERLEAVED_DEPTHS,  VIEW_TYPE_2D_ARRAY, 0, 4, 1, 1) \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_DEPTHS_2,  DEINTERLEAVED_DEPTHS,  VIEW_TYPE_2D_ARRAY, 0, 4, 2, 1) \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_DEPTHS_3,  DEINTERLEAVED_DEPTHS,  VIEW_TYPE_2D_ARRAY, 0, 4, 3, 1) \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_NORMALS,   DEINTERLEAVED_NORMALS, VIEW_TYPE_2D_ARRAY, 0, 1, 0, 4) \
	SHADER_RESOURCE_VIEW(IMPORTANCE_MAP,          IMPORTANCE_MAP,        VIEW_TYPE_2D,       0, 1, 0, 1) \
	SHADER_RESOURCE_VIEW(IMPORTANCE_MAP_PONG,     IMPORTANCE_MAP_PONG,   VIEW_TYPE_2D,       0, 1, 0, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PING,        SSAO_BUFFER_PING,      VIEW_TYPE_2D_ARRAY, 0, 1, 0, 4) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PING_0,      SSAO_BUFFER_PING,      VIEW_TYPE_2D_ARRAY, 0, 1, 0, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PING_1,      SSAO_BUFFER_PING,      VIEW_TYPE_2D_ARRAY, 0, 1, 1, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PING_2,      SSAO_BUFFER_PING,      VIEW_TYPE_2D_ARRAY, 0, 1, 2, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PING_3,      SSAO_BUFFER_PING,      VIEW_TYPE_2D_ARRAY, 0, 1, 3, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PONG,        SSAO_BUFFER_PONG,      VIEW_TYPE_2D_ARRAY, 0, 1, 0, 4) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PONG_0,      SSAO_BUFFER_PONG,      VIEW_TYPE_2D_ARRAY, 0, 1, 0, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PONG_1,      SSAO_BUFFER_PONG,      VIEW_TYPE_2D_ARRAY, 0, 1, 1, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PONG_2,      SSAO_BUFFER_PONG,      VIEW_TYPE_2D_ARRAY, 0, 1, 2, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PONG_3,      SSAO_BUFFER_PONG,      VIEW_TYPE_2D_ARRAY, 0, 1, 3, 1)

typedef enum ShaderResourceViewID {
#define SHADER_RESOURCE_VIEW(name, _texture, _view_dimension, _most_detailed_mip, _mip_levels, _first_array_slice, _array_size) SRV_##name,
	SHADER_RESOURCE_VIEWS
#undef SHADER_RESOURCE_VIEW
	NUM_SHADER_RESOURCE_VIEWS
} ShaderResourceViewID;

typedef struct ShaderResourceViewMetaData {
	TextureID       texture;
	ViewTypeID      viewType;
	uint32_t        mostDetailedMip;
	uint32_t        mipLevels;
	uint32_t        firstArraySlice;
	uint32_t        arraySize;
} ShaderResourceViewMetaData;

static const ShaderResourceViewMetaData SRV_META_DATA[NUM_SHADER_RESOURCE_VIEWS] = {
#define SHADER_RESOURCE_VIEW(_name, texture, view_dimension, most_detailed_mip, mip_levels, first_array_slice, array_size) { TEXTURE_##texture, view_dimension, most_detailed_mip, mip_levels, first_array_slice, array_size },
	SHADER_RESOURCE_VIEWS
#undef SHADER_RESOURCE_VIEW
};

// UNORDERED_ACCESS_VIEW(name, texture, view_dimension, mip_slice, first_array_slice, array_size)
#define UNORDERED_ACCESS_VIEWS \
	UNORDERED_ACCESS_VIEW(DEINTERLEAVED_DEPTHS_MIP_0, DEINTERLEAVED_DEPTHS,  VIEW_TYPE_2D_ARRAY, 0, 0, 4) \
	UNORDERED_ACCESS_VIEW(DEINTERLEAVED_DEPTHS_MIP_1, DEINTERLEAVED_DEPTHS,  VIEW_TYPE_2D_ARRAY, 1, 0, 4) \
	UNORDERED_ACCESS_VIEW(DEINTERLEAVED_DEPTHS_MIP_2, DEINTERLEAVED_DEPTHS,  VIEW_TYPE_2D_ARRAY, 2, 0, 4) \
	UNORDERED_ACCESS_VIEW(DEINTERLEAVED_DEPTHS_MIP_3, DEINTERLEAVED_DEPTHS,  VIEW_TYPE_2D_ARRAY, 3, 0, 4) \
	UNORDERED_ACCESS_VIEW(DEINTERLEAVED_NORMALS,      DEINTERLEAVED_NORMALS, VIEW_TYPE_2D_ARRAY, 0, 0, 4) \
	UNORDERED_ACCESS_VIEW(IMPORTANCE_MAP,             IMPORTANCE_MAP,        VIEW_TYPE_2D,       0, 0, 1) \
	UNORDERED_ACCESS_VIEW(IMPORTANCE_MAP_PONG,        IMPORTANCE_MAP_PONG,   VIEW_TYPE_2D,       0, 0, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PING,           SSAO_BUFFER_PING,      VIEW_TYPE_2D_ARRAY, 0, 0, 4) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PING_0,         SSAO_BUFFER_PING,      VIEW_TYPE_2D_ARRAY, 0, 0, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PING_1,         SSAO_BUFFER_PING,      VIEW_TYPE_2D_ARRAY, 0, 1, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PING_2,         SSAO_BUFFER_PING,      VIEW_TYPE_2D_ARRAY, 0, 2, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PING_3,         SSAO_BUFFER_PING,      VIEW_TYPE_2D_ARRAY, 0, 3, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PONG,           SSAO_BUFFER_PONG,      VIEW_TYPE_2D_ARRAY, 0, 0, 4) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PONG_0,         SSAO_BUFFER_PONG,      VIEW_TYPE_2D_ARRAY, 0, 0, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PONG_1,         SSAO_BUFFER_PONG,      VIEW_TYPE_2D_ARRAY, 0, 1, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PONG_2,         SSAO_BUFFER_PONG,      VIEW_TYPE_2D_ARRAY, 0, 2, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PONG_3,         SSAO_BUFFER_PONG,      VIEW_TYPE_2D_ARRAY, 0, 3, 1)

typedef enum UnorderedAccessViewID {
#define UNORDERED_ACCESS_VIEW(name, _texture, _view_dimension, _mip_slice, _first_array_slice, _array_size) UAV_##name,
	UNORDERED_ACCESS_VIEWS
#undef UNORDERED_ACCESS_VIEW
	NUM_UNORDERED_ACCESS_VIEWS
} UnorderedAccessViewID;

typedef struct UnorderedAccessViewMetaData {
	TextureID   textureID;
	ViewTypeID  viewType;
	uint32_t    mostDetailedMip;
	uint32_t    firstArraySlice;
	uint32_t    arraySize;
} UnorderedAccessViewMetaData;

static const UnorderedAccessViewMetaData UAV_META_DATA[NUM_UNORDERED_ACCESS_VIEWS] = {
#define UNORDERED_ACCESS_VIEW(_name, texture, view_dimension, mip_slice, first_array_slice, array_size) { TEXTURE_##texture, view_dimension, mip_slice, first_array_slice, array_size },
	UNORDERED_ACCESS_VIEWS
#undef UNORDERED_ACCESS_VIEW
};

// INPUT_DESCRIPTOR(descriptor_set_name, srv_name, binding_num)
#define INPUT_DESCRIPTOR_BINDINGS \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_0,     DEINTERLEAVED_DEPTHS_0, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_0,     DEINTERLEAVED_NORMALS,  1) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_1,     DEINTERLEAVED_DEPTHS_1, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_1,     DEINTERLEAVED_NORMALS,  1) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_2,     DEINTERLEAVED_DEPTHS_2, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_2,     DEINTERLEAVED_NORMALS,  1) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_3,     DEINTERLEAVED_DEPTHS_3, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_3,     DEINTERLEAVED_NORMALS,  1) \
	\
	INPUT_DESCRIPTOR_BINDING(GENERATE_0,  DEINTERLEAVED_DEPTHS_0, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_0,  DEINTERLEAVED_NORMALS,  1) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_1,  DEINTERLEAVED_DEPTHS_1, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_1,  DEINTERLEAVED_NORMALS,  1) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_2,  DEINTERLEAVED_DEPTHS_2, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_2,  DEINTERLEAVED_NORMALS,  1) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_3,  DEINTERLEAVED_DEPTHS_3, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_3,  DEINTERLEAVED_NORMALS,  1) \
	\
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_0,  DEINTERLEAVED_DEPTHS_0, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_0,  DEINTERLEAVED_NORMALS,  1) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_0,  IMPORTANCE_MAP,         3) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_0,  SSAO_BUFFER_PONG_0,     4) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_1,  DEINTERLEAVED_DEPTHS_1, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_1,  DEINTERLEAVED_NORMALS,  1) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_1,  IMPORTANCE_MAP,         3) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_1,  SSAO_BUFFER_PONG_1,     4) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_2,  DEINTERLEAVED_DEPTHS_2, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_2,  DEINTERLEAVED_NORMALS,  1) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_2,  IMPORTANCE_MAP,         3) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_2,  SSAO_BUFFER_PONG_2,     4) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_3,  DEINTERLEAVED_DEPTHS_3, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_3,  DEINTERLEAVED_NORMALS,  1) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_3,  IMPORTANCE_MAP,         3) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_3,  SSAO_BUFFER_PONG_3,     4) \
	\
	INPUT_DESCRIPTOR_BINDING(GENERATE_IMPORTANCE_MAP,      SSAO_BUFFER_PONG,       0) \
	INPUT_DESCRIPTOR_BINDING(POSTPROCESS_IMPORTANCE_MAP_A, IMPORTANCE_MAP,         0) \
	INPUT_DESCRIPTOR_BINDING(POSTPROCESS_IMPORTANCE_MAP_B, IMPORTANCE_MAP_PONG,    0) \
	\
	INPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_0, SSAO_BUFFER_PING_0, 0) \
	INPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_1, SSAO_BUFFER_PING_1, 0) \
	INPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_2, SSAO_BUFFER_PING_2, 0) \
	INPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_3, SSAO_BUFFER_PING_3, 0) \
	\
	INPUT_DESCRIPTOR_BINDING(BILATERAL_UPSAMPLE_PING, SSAO_BUFFER_PING,     0) \
	INPUT_DESCRIPTOR_BINDING(BILATERAL_UPSAMPLE_PING, DEINTERLEAVED_DEPTHS, 2) \
	INPUT_DESCRIPTOR_BINDING(BILATERAL_UPSAMPLE_PONG, SSAO_BUFFER_PONG,     0) \
	INPUT_DESCRIPTOR_BINDING(BILATERAL_UPSAMPLE_PONG, DEINTERLEAVED_DEPTHS, 2) \
	\
	INPUT_DESCRIPTOR_BINDING(APPLY_PING, SSAO_BUFFER_PING, 0) \
	INPUT_DESCRIPTOR_BINDING(APPLY_PONG, SSAO_BUFFER_PONG, 0)

// need this to define NUM_INPUT_DESCRIPTOR_BINDINGS
typedef enum InputDescriptorBindingID {
#define INPUT_DESCRIPTOR_BINDING(descriptor_set_name, srv_name, _binding_num) INPUT_DESCRIPTOR_BINDING_##descriptor_set_name##_##srv_name,
	INPUT_DESCRIPTOR_BINDINGS
#undef INPUT_DESCRIPTOR_BINDING
	NUM_INPUT_DESCRIPTOR_BINDINGS
} InputDescriptorBindingID;

typedef struct InputDescriptorBindingMetaData {
	DescriptorSetID      descriptorID;
	ShaderResourceViewID srvID;
	uint32_t             bindingNumber;
} InputDescriptorBindingMetaData;

static const InputDescriptorBindingMetaData INPUT_DESCRIPTOR_BINDING_META_DATA[NUM_INPUT_DESCRIPTOR_BINDINGS] = {
#define INPUT_DESCRIPTOR_BINDING(descriptor_set_name, srv_name, binding_num) { DS_##descriptor_set_name, SRV_##srv_name, binding_num },
	INPUT_DESCRIPTOR_BINDINGS
#undef INPUT_DESCRIPTOR_BINDING
};

// OUTPUT_DESCRIPTOR(descriptor_set_name, uav_name, binding_num)
#define OUTPUT_DESCRIPTOR_BINDINGS \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_DEPTHS,                     DEINTERLEAVED_DEPTHS_MIP_0, 0) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_DEPTHS_MIPS,                DEINTERLEAVED_DEPTHS_MIP_0, 0) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_DEPTHS_MIPS,                DEINTERLEAVED_DEPTHS_MIP_1, 1) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_DEPTHS_MIPS,                DEINTERLEAVED_DEPTHS_MIP_2, 2) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_DEPTHS_MIPS,                DEINTERLEAVED_DEPTHS_MIP_3, 3) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_NORMALS,                    DEINTERLEAVED_NORMALS,      0) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_NORMALS_FROM_INPUT_NORMALS, DEINTERLEAVED_NORMALS,      0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_0,           SSAO_BUFFER_PONG_0,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_1,           SSAO_BUFFER_PONG_1,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_2,           SSAO_BUFFER_PONG_2,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_3,           SSAO_BUFFER_PONG_3,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_0,                         SSAO_BUFFER_PING_0,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_1,                         SSAO_BUFFER_PING_1,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_2,                         SSAO_BUFFER_PING_2,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_3,                         SSAO_BUFFER_PING_3,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_0,                SSAO_BUFFER_PING_0,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_1,                SSAO_BUFFER_PING_1,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_2,                SSAO_BUFFER_PING_2,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_3,                SSAO_BUFFER_PING_3,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_IMPORTANCE_MAP,            IMPORTANCE_MAP,             0) \
	OUTPUT_DESCRIPTOR_BINDING(POSTPROCESS_IMPORTANCE_MAP_A,       IMPORTANCE_MAP_PONG,        0) \
	OUTPUT_DESCRIPTOR_BINDING(POSTPROCESS_IMPORTANCE_MAP_B,       IMPORTANCE_MAP,             0) \
	OUTPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_0,              SSAO_BUFFER_PONG_0,         0) \
	OUTPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_1,              SSAO_BUFFER_PONG_1,         0) \
	OUTPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_2,              SSAO_BUFFER_PONG_2,         0) \
	OUTPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_3,              SSAO_BUFFER_PONG_3,         0)

typedef enum OutputDescriptorBindingID {
#define OUTPUT_DESCRIPTOR_BINDING(descriptor_set_name, uav_name, _binding_num) OUTPUT_DESCRIPTOR_BINDING_##descriptor_set_name##_##uav_name,
	OUTPUT_DESCRIPTOR_BINDINGS
#undef OUTPUT_DESCRIPTOR_BINDING
	NUM_OUTPUT_DESCRIPTOR_BINDINGS
} OutputDescriptorBindingID;

typedef struct OutputDescriptorBindingMetaData {
	DescriptorSetID      descriptorID;
	UnorderedAccessViewID uavID;
	uint32_t              bindingNumber;
} OutputDescriptorBindingMetaData;

static const OutputDescriptorBindingMetaData OUTPUT_DESCRIPTOR_BINDING_META_DATA[NUM_OUTPUT_DESCRIPTOR_BINDINGS] = {
#define OUTPUT_DESCRIPTOR_BINDING(descriptor_set_name, uav_name, binding_num) { DS_##descriptor_set_name, UAV_##uav_name, binding_num },
	OUTPUT_DESCRIPTOR_BINDINGS
#undef OUTPUT_DESCRIPTOR_BINDING
};

// define all the data for compute shaders
// COMPUTE_SHADER(enum_name, pascal_case_name, descriptor_set)
#define COMPUTE_SHADERS \
	COMPUTE_SHADER(CLEAR_LOAD_COUNTER,                             ClearLoadCounter,                          CLEAR_LOAD_COUNTER) \
	\
	COMPUTE_SHADER(PREPARE_DOWNSAMPLED_DEPTHS,                     PrepareDownsampledDepths,                  PREPARE_DEPTHS) \
	COMPUTE_SHADER(PREPARE_NATIVE_DEPTHS,                          PrepareNativeDepths,                       PREPARE_DEPTHS) \
	COMPUTE_SHADER(PREPARE_DOWNSAMPLED_DEPTHS_AND_MIPS,            PrepareDownsampledDepthsAndMips,           PREPARE_DEPTHS_MIPS) \
	COMPUTE_SHADER(PREPARE_NATIVE_DEPTHS_AND_MIPS,                 PrepareNativeDepthsAndMips,                PREPARE_DEPTHS_MIPS) \
	COMPUTE_SHADER(PREPARE_DOWNSAMPLED_NORMALS,                    PrepareDownsampledNormals,                 PREPARE_NORMALS) \
	COMPUTE_SHADER(PREPARE_NATIVE_NORMALS,                         PrepareNativeNormals,                      PREPARE_NORMALS) \
	COMPUTE_SHADER(PREPARE_DOWNSAMPLED_NORMALS_FROM_INPUT_NORMALS, PrepareDownsampledNormalsFromInputNormals, PREPARE_NORMALS_FROM_INPUT_NORMALS) \
	COMPUTE_SHADER(PREPARE_NATIVE_NORMALS_FROM_INPUT_NORMALS,      PrepareNativeNormalsFromInputNormals,      PREPARE_NORMALS_FROM_INPUT_NORMALS) \
	COMPUTE_SHADER(PREPARE_DOWNSAMPLED_DEPTHS_HALF,                PrepareDownsampledDepthsHalf,              PREPARE_DEPTHS) \
	COMPUTE_SHADER(PREPARE_NATIVE_DEPTHS_HALF,                     PrepareNativeDepthsHalf,                   PREPARE_DEPTHS) \
	\
	COMPUTE_SHADER(GENERATE_Q0,                                    GenerateQ0,                                GENERATE) \
	COMPUTE_SHADER(GENERATE_Q1,                                    GenerateQ1,                                GENERATE) \
	COMPUTE_SHADER(GENERATE_Q2,                                    GenerateQ2,                                GENERATE) \
	COMPUTE_SHADER(GENERATE_Q3,                                    GenerateQ3,                                GENERATE_ADAPTIVE) \
	COMPUTE_SHADER(GENERATE_Q3_BASE,                               GenerateQ3Base,                            GENERATE) \
	\
	COMPUTE_SHADER(GENERATE_IMPORTANCE_MAP,                        GenerateImportanceMap,                     GENERATE_IMPORTANCE_MAP) \
	COMPUTE_SHADER(POSTPROCESS_IMPORTANCE_MAP_A,                   PostprocessImportanceMapA,                 POSTPROCESS_IMPORTANCE_MAP_A) \
	COMPUTE_SHADER(POSTPROCESS_IMPORTANCE_MAP_B,                   PostprocessImportanceMapB,                 POSTPROCESS_IMPORTANCE_MAP_B) \
	\
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_1,                          EdgeSensitiveBlur1,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_2,                          EdgeSensitiveBlur2,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_3,                          EdgeSensitiveBlur3,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_4,                          EdgeSensitiveBlur4,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_5,                          EdgeSensitiveBlur5,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_6,                          EdgeSensitiveBlur6,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_7,                          EdgeSensitiveBlur7,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_8,                          EdgeSensitiveBlur8,                        EDGE_SENSITIVE_BLUR) \
	\
	COMPUTE_SHADER(APPLY,                                          Apply,                                     APPLY) \
	COMPUTE_SHADER(NON_SMART_APPLY,                                NonSmartApply,                             APPLY) \
	COMPUTE_SHADER(NON_SMART_HALF_APPLY,                           NonSmartHalfApply,                         APPLY) \
	\
	COMPUTE_SHADER(UPSCALE_BILATERAL_5X5_SMART,                    UpscaleBilateral5x5Smart,                  BILATERAL_UPSAMPLE) \
	COMPUTE_SHADER(UPSCALE_BILATERAL_5X5_NON_SMART,                UpscaleBilateral5x5NonSmart,               BILATERAL_UPSAMPLE) \
	COMPUTE_SHADER(UPSCALE_BILATERAL_5X5_HALF,                     UpscaleBilateral5x5Half,                   BILATERAL_UPSAMPLE)

typedef enum ComputeShaderID {
#define COMPUTE_SHADER(name, _pascal_name, _descriptor_set) CS_##name,
	COMPUTE_SHADERS
#undef COMPUTE_SHADER
	NUM_COMPUTE_SHADERS
} ComputeShaderID;

typedef struct ComputeShaderMetaData {
	const char            *name;
	DescriptorSetLayoutID  descriptorSetLayoutID;
	const char            *objectName;
	const char            *rootSignatureName;
} ComputeShaderMetaData;

typedef struct ComputeShaderSPIRV {
	const uint32_t *spirv;
	size_t          len;
} ComputeShaderSPIRV;

static const char *COMPUTE_SHADER_SPIRV_32[] = {
#define COMPUTE_SHADER(name, pascal_name, descriptor_set_layout) "builtin://shaders/post/ffx-cacao/CACAO" #pascal_name "_32.spv",
	COMPUTE_SHADERS
#undef COMPUTE_SHADER
};

#define NUM_SAMPLERS 5

struct FFX_CACAO_GraniteContext
{
	FFX_CACAO_Settings settings = {};
	FFX_CACAO_Bool useDownsampledSsao = {};
	FFX_CACAO_BufferSizeInfo bufferSizeInfo = {};

	Vulkan::Device *device = nullptr;
	Vulkan::Program *computePipelines[NUM_COMPUTE_SHADERS] = {};
	Vulkan::SamplerHandle samplers[NUM_SAMPLERS];

	Vulkan::ImageHandle textures[NUM_TEXTURES];
	Vulkan::ImageViewHandle shaderResourceViews[NUM_SHADER_RESOURCE_VIEWS];
	Vulkan::ImageViewHandle unorderedAccessViews[NUM_UNORDERED_ACCESS_VIEWS];
	Vulkan::ImageHandle loadCounter;

	const Vulkan::ImageView *depthView = nullptr;
	const Vulkan::ImageView *normalsView = nullptr;
	const Vulkan::ImageView *outputView = nullptr;
};

#ifdef __cplusplus
extern "C"
{
#endif

FFX_CACAO_Status FFX_CACAO_GraniteAllocContext(FFX_CACAO_GraniteContext** context, const FFX_CACAO_GraniteCreateInfo *info)
{
	if (!context || !info)
		return FFX_CACAO_STATUS_INVALID_POINTER;
	auto ctx = std::make_unique<FFX_CACAO_GraniteContext>();

	ctx->device = info->device;

	{
		uint32_t numSamplersInited = 0;
		Vulkan::SamplerCreateInfo samplerCreateInfo = {};
		samplerCreateInfo.mag_filter = VK_FILTER_LINEAR;
		samplerCreateInfo.min_filter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerCreateInfo.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.mip_lod_bias = 0.0f;
		samplerCreateInfo.anisotropy_enable = VK_FALSE;
		samplerCreateInfo.compare_enable = VK_FALSE;
		samplerCreateInfo.min_lod = -1000.0f;
		samplerCreateInfo.max_lod = 1000.0f;
		samplerCreateInfo.unnormalized_coordinates = VK_FALSE;

		ctx->samplers[numSamplersInited] = ctx->device->create_sampler(samplerCreateInfo);
		numSamplersInited++;

		samplerCreateInfo.address_mode_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerCreateInfo.address_mode_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerCreateInfo.address_mode_w = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;

		ctx->samplers[numSamplersInited] = ctx->device->create_sampler(samplerCreateInfo);
		numSamplersInited++;

		samplerCreateInfo.mag_filter = VK_FILTER_LINEAR;
		samplerCreateInfo.min_filter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		ctx->samplers[numSamplersInited] = ctx->device->create_sampler(samplerCreateInfo);
		numSamplersInited++;

		samplerCreateInfo.mag_filter = VK_FILTER_NEAREST;
		samplerCreateInfo.min_filter = VK_FILTER_NEAREST;
		samplerCreateInfo.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		ctx->samplers[numSamplersInited] = ctx->device->create_sampler(samplerCreateInfo);
		numSamplersInited++;

		samplerCreateInfo.mag_filter = VK_FILTER_NEAREST;
		samplerCreateInfo.min_filter = VK_FILTER_NEAREST;
		samplerCreateInfo.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerCreateInfo.address_mode_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerCreateInfo.address_mode_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerCreateInfo.address_mode_w = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;

		ctx->samplers[numSamplersInited] = ctx->device->create_sampler(samplerCreateInfo);
		numSamplersInited++;
	}

	for (uint32_t numShaderModulesInited = 0; numShaderModulesInited < NUM_COMPUTE_SHADERS; numShaderModulesInited++)
	{
		const char *path = COMPUTE_SHADER_SPIRV_32[numShaderModulesInited];
		auto *program = ctx->device->get_shader_manager().register_compute(path);
		ctx->computePipelines[numShaderModulesInited] = program->register_variant({})->get_program();
	}

	// Create load counter VkImage
	{
		Vulkan::ImageCreateInfo image_info = {};

		image_info.type = VK_IMAGE_TYPE_1D;
		image_info.format = VK_FORMAT_R32_UINT;
		image_info.width = 1;
		image_info.height = 1;
		image_info.depth = 1;
		image_info.levels = 1;
		image_info.layers = 1;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image_info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		ctx->loadCounter = ctx->device->create_image(image_info);
		ctx->loadCounter->set_layout(Vulkan::Layout::General);
	}

	*context = ctx.release();
	return FFX_CACAO_STATUS_OK;
}

FFX_CACAO_Status FFX_CACAO_GraniteDestroyContext(FFX_CACAO_GraniteContext* context)
{
	delete context;
	return FFX_CACAO_STATUS_OK;
}

FFX_CACAO_Status FFX_CACAO_GraniteInitScreenSizeDependentResources(FFX_CACAO_GraniteContext* context,
																   const FFX_CACAO_GraniteScreenSizeInfo* info)
{
	if (!context || !info)
		return FFX_CACAO_STATUS_INVALID_POINTER;

	FFX_CACAO_Bool useDownsampledSsao = info->useDownsampledSsao;
	context->useDownsampledSsao = useDownsampledSsao;
	context->depthView = info->depthView;
	context->normalsView = info->normalsView;
	context->outputView = info->outputView;

	FFX_CACAO_BufferSizeInfo *bsi = &context->bufferSizeInfo;
	FFX_CACAO_UpdateBufferSizeInfo(info->width, info->height, useDownsampledSsao, bsi);

	for (uint32_t numTextureImagesInited = 0; numTextureImagesInited < NUM_TEXTURES; ++numTextureImagesInited)
	{
		TextureMetaData metaData = TEXTURE_META_DATA[numTextureImagesInited];

		Vulkan::ImageCreateInfo image_info = {};
		image_info.type = VK_IMAGE_TYPE_2D;
		image_info.format = TEXTURE_FORMAT_LOOKUP_VK[metaData.format];
		memcpy(&image_info.width, reinterpret_cast<const uint8_t*>(bsi) + metaData.widthOffset, sizeof(uint32_t));
		memcpy(&image_info.height, reinterpret_cast<const uint8_t*>(bsi) + metaData.heightOffset, sizeof(uint32_t));
		image_info.depth = 1;
		image_info.levels = metaData.numMips;
		image_info.layers = metaData.arraySize;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image_info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		context->textures[numTextureImagesInited] = context->device->create_image(image_info);
		context->textures[numTextureImagesInited]->set_layout(Vulkan::Layout::General);
	}

	for (uint32_t numSrvsInited = 0; numSrvsInited < NUM_SHADER_RESOURCE_VIEWS; numSrvsInited++)
	{
		auto &srvMetaData = SRV_META_DATA[numSrvsInited];
		Vulkan::ImageViewCreateInfo view_info = {};

		view_info.image = context->textures[srvMetaData.texture].get();
		view_info.view_type = VIEW_TYPE_LOOKUP_VK[srvMetaData.viewType];
		view_info.format = TEXTURE_FORMAT_LOOKUP_VK[TEXTURE_META_DATA[srvMetaData.texture].format];
		view_info.base_level = srvMetaData.mostDetailedMip;
		view_info.levels = srvMetaData.mipLevels;
		view_info.base_layer = srvMetaData.firstArraySlice;
		view_info.layers = srvMetaData.arraySize;
		context->shaderResourceViews[numSrvsInited] = context->device->create_image_view(view_info);
	}

	for (uint32_t numUavsInited = 0; numUavsInited < NUM_UNORDERED_ACCESS_VIEWS; numUavsInited++)
	{
		auto &uavMetaData = UAV_META_DATA[numUavsInited];
		Vulkan::ImageViewCreateInfo view_info = {};

		view_info.image = context->textures[uavMetaData.textureID].get();
		view_info.view_type = VIEW_TYPE_LOOKUP_VK[uavMetaData.viewType];
		view_info.format = TEXTURE_FORMAT_LOOKUP_VK[TEXTURE_META_DATA[uavMetaData.textureID].format];
		view_info.base_level = uavMetaData.mostDetailedMip;
		view_info.levels = 1;
		view_info.base_layer = uavMetaData.firstArraySlice;
		view_info.layers = uavMetaData.arraySize;
		context->unorderedAccessViews[numUavsInited] = context->device->create_image_view(view_info);
	}

	return FFX_CACAO_STATUS_OK;
}

FFX_CACAO_Status FFX_CACAO_GraniteDestroyScreenSizeDependentResources(FFX_CACAO_GraniteContext* context)
{
	if (!context)
		return FFX_CACAO_STATUS_INVALID_POINTER;

	for (auto &view : context->shaderResourceViews)
		view.reset();
	for (auto &view : context->unorderedAccessViews)
		view.reset();
	for (auto &img : context->textures)
		img.reset();

	return FFX_CACAO_STATUS_OK;
}

FFX_CACAO_Status FFX_CACAO_GraniteUpdateSettings(FFX_CACAO_GraniteContext* context, const FFX_CACAO_Settings* settings)
{
	if (!context || !settings)
		return FFX_CACAO_STATUS_INVALID_POINTER;
	memcpy(&context->settings, settings, sizeof(*settings));
	return FFX_CACAO_STATUS_OK;
}

static inline void computeDispatch(FFX_CACAO_GraniteContext* context, Vulkan::CommandBuffer &cb,
								   ComputeShaderID cs,
								   uint32_t width, uint32_t height, uint32_t depth)
{
	cb.set_program(context->computePipelines[cs]);
	cb.dispatch(width, height, depth);
}

static inline void setupDescriptors(FFX_CACAO_GraniteContext *context, Vulkan::CommandBuffer &cb,
                                    DescriptorSetID ds, const FFX_CACAO_Constants *constantBank)
{
	// DXC emits b0 -> 10, t0 -> 20, u0 -> 30 mappings.
	memcpy(cb.allocate_typed_constant_data<FFX_CACAO_Constants>(1, 0, 1),
	       &constantBank[DESCRIPTOR_SET_META_DATA[ds].pass], sizeof(FFX_CACAO_Constants));

	// TODO: Could pre-unroll these.
	for (auto &bindingMetaData : INPUT_DESCRIPTOR_BINDING_META_DATA)
		if (bindingMetaData.descriptorID == ds)
			cb.set_texture(2, bindingMetaData.bindingNumber, *context->shaderResourceViews[bindingMetaData.srvID]);

	for (auto &bindingMetaData : OUTPUT_DESCRIPTOR_BINDING_META_DATA)
		if (bindingMetaData.descriptorID == ds)
			cb.set_storage_texture(2, 8 + bindingMetaData.bindingNumber, *context->unorderedAccessViews[bindingMetaData.uavID]);

	// Set up inputs and outputs which depend on user input and output.
	switch (ds)
	{
	case DS_PREPARE_DEPTHS:
	case DS_PREPARE_DEPTHS_MIPS:
	case DS_PREPARE_NORMALS:
		cb.set_texture(2, 0, *context->depthView);
		break;

	case DS_BILATERAL_UPSAMPLE_PING:
	case DS_BILATERAL_UPSAMPLE_PONG:
		cb.set_texture(2, 1, *context->depthView);
		cb.set_storage_texture(2, 8, *context->outputView);
		break;

	case DS_APPLY_PING:
	case DS_APPLY_PONG:
		cb.set_storage_texture(2, 8, *context->outputView);
		break;

	case DS_POSTPROCESS_IMPORTANCE_MAP_B:
		cb.set_storage_texture(2, 9, context->loadCounter->get_view());
		break;

	case DS_CLEAR_LOAD_COUNTER:
		cb.set_storage_texture(2, 8, context->loadCounter->get_view());
		break;

	case DS_GENERATE_ADAPTIVE_0:
	case DS_GENERATE_ADAPTIVE_1:
	case DS_GENERATE_ADAPTIVE_2:
	case DS_GENERATE_ADAPTIVE_3:
		cb.set_texture(2, 2, context->loadCounter->get_view());
		break;

	case DS_PREPARE_NORMALS_FROM_INPUT_NORMALS:
		if (context->normalsView)
			cb.set_texture(2, 0, *context->normalsView);
		break;

	default:
		break;
	}
}

static inline void computeBarrier(Vulkan::CommandBuffer &cb)
{
	cb.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	           VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

FFX_CACAO_Status FFX_CACAO_GraniteDraw(FFX_CACAO_GraniteContext* context, Vulkan::CommandBuffer &cb,
                                       const FFX_CACAO_Matrix4x4* proj,
                                       const FFX_CACAO_Matrix4x4* normalsToView)
{
	if (!context || !proj)
		return FFX_CACAO_STATUS_INVALID_POINTER;

	FFX_CACAO_BufferSizeInfo *bsi = &context->bufferSizeInfo;

	FFX_CACAO_Constants constants[4];
	for (int i = 0; i < 4; i++)
	{
		FFX_CACAO_UpdateConstants(&constants[i], &context->settings, bsi, proj, normalsToView);
		FFX_CACAO_UpdatePerPassConstants(&constants[i], &context->settings, bsi, i);
	}

	for (uint32_t i = 0; i < FFX_CACAO_ARRAY_SIZE(context->samplers); i++)
		cb.set_sampler(0, i, *context->samplers[i]);

	cb.begin_region("FidelityFX CACAO");

	// Prepare depths, normals and mips
	{
		cb.begin_region("Prepare downsampled depths, normals and mips");

		setupDescriptors(context, cb, DS_CLEAR_LOAD_COUNTER, constants);
		computeDispatch(context, cb, CS_CLEAR_LOAD_COUNTER, 1, 1, 1);

		switch (context->settings.qualityLevel)
		{
		case FFX_CACAO_QUALITY_LOWEST: {
			uint32_t dispatchWidth = dispatchSize(FFX_CACAO_PREPARE_DEPTHS_HALF_WIDTH, bsi->deinterleavedDepthBufferWidth);
			uint32_t dispatchHeight = dispatchSize(FFX_CACAO_PREPARE_DEPTHS_HALF_HEIGHT, bsi->deinterleavedDepthBufferHeight);
			ComputeShaderID csPrepareDepthsHalf = context->useDownsampledSsao ? CS_PREPARE_DOWNSAMPLED_DEPTHS_HALF : CS_PREPARE_NATIVE_DEPTHS_HALF;
			setupDescriptors(context, cb, DS_PREPARE_DEPTHS, constants);
			computeDispatch(context, cb, csPrepareDepthsHalf, dispatchWidth, dispatchHeight, 1);
			break;
		}
		case FFX_CACAO_QUALITY_LOW: {
			uint32_t dispatchWidth = dispatchSize(FFX_CACAO_PREPARE_DEPTHS_WIDTH, bsi->deinterleavedDepthBufferWidth);
			uint32_t dispatchHeight = dispatchSize(FFX_CACAO_PREPARE_DEPTHS_HEIGHT, bsi->deinterleavedDepthBufferHeight);
			ComputeShaderID csPrepareDepths = context->useDownsampledSsao ? CS_PREPARE_DOWNSAMPLED_DEPTHS : CS_PREPARE_NATIVE_DEPTHS;
			setupDescriptors(context, cb, DS_PREPARE_DEPTHS, constants);
			computeDispatch(context, cb, csPrepareDepths, dispatchWidth, dispatchHeight, 1);
			break;
		}
		default: {
			uint32_t dispatchWidth = dispatchSize(FFX_CACAO_PREPARE_DEPTHS_AND_MIPS_WIDTH, bsi->deinterleavedDepthBufferWidth);
			uint32_t dispatchHeight = dispatchSize(FFX_CACAO_PREPARE_DEPTHS_AND_MIPS_HEIGHT, bsi->deinterleavedDepthBufferHeight);
			ComputeShaderID csPrepareDepthsAndMips = context->useDownsampledSsao ? CS_PREPARE_DOWNSAMPLED_DEPTHS_AND_MIPS : CS_PREPARE_NATIVE_DEPTHS_AND_MIPS;
			setupDescriptors(context, cb, DS_PREPARE_DEPTHS_MIPS, constants);
			computeDispatch(context, cb, csPrepareDepthsAndMips, dispatchWidth, dispatchHeight, 1);
			break;
		}
		}

		if (context->settings.generateNormals)
		{
			uint32_t dispatchWidth = dispatchSize(FFX_CACAO_PREPARE_NORMALS_WIDTH, bsi->ssaoBufferWidth);
			uint32_t dispatchHeight = dispatchSize(FFX_CACAO_PREPARE_NORMALS_HEIGHT, bsi->ssaoBufferHeight);
			ComputeShaderID csPrepareNormals = context->useDownsampledSsao ? CS_PREPARE_DOWNSAMPLED_NORMALS : CS_PREPARE_NATIVE_NORMALS;
			setupDescriptors(context, cb, DS_PREPARE_NORMALS, constants);
			computeDispatch(context, cb, csPrepareNormals, dispatchWidth, dispatchHeight, 1);
		}
		else
		{
			uint32_t dispatchWidth = dispatchSize(PREPARE_NORMALS_FROM_INPUT_NORMALS_WIDTH, bsi->ssaoBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_NORMALS_FROM_INPUT_NORMALS_HEIGHT, bsi->ssaoBufferHeight);
			ComputeShaderID csPrepareNormalsFromInputNormals = context->useDownsampledSsao ? CS_PREPARE_DOWNSAMPLED_NORMALS_FROM_INPUT_NORMALS : CS_PREPARE_NATIVE_NORMALS_FROM_INPUT_NORMALS;
			setupDescriptors(context, cb, DS_PREPARE_NORMALS_FROM_INPUT_NORMALS, constants);
			computeDispatch(context, cb, csPrepareNormalsFromInputNormals, dispatchWidth, dispatchHeight, 1);
		}

		cb.end_region();
	}

	computeBarrier(cb);

	// base pass for highest quality setting
	if (context->settings.qualityLevel == FFX_CACAO_QUALITY_HIGHEST)
	{
		cb.begin_region("Generate High Quality Base Pass");

		// SSAO
		{
			cb.begin_region("Base SSAO");

			uint32_t dispatchWidth = dispatchSize(FFX_CACAO_GENERATE_WIDTH, bsi->ssaoBufferWidth);
			uint32_t dispatchHeight = dispatchSize(FFX_CACAO_GENERATE_HEIGHT, bsi->ssaoBufferHeight);

			for (int pass = 0; pass < 4; ++pass)
			{
				setupDescriptors(context, cb, DescriptorSetID(DS_GENERATE_ADAPTIVE_BASE_0 + pass),
				                 constants);
				computeDispatch(context, cb, CS_GENERATE_Q3_BASE, dispatchWidth, dispatchHeight, 1);
			}

			cb.end_region();
		}

		computeBarrier(cb);

		// generate importance map
		{
			cb.begin_region("Importance Map");

			uint32_t dispatchWidth = dispatchSize(IMPORTANCE_MAP_WIDTH, bsi->importanceMapWidth);
			uint32_t dispatchHeight = dispatchSize(IMPORTANCE_MAP_HEIGHT, bsi->importanceMapHeight);

			setupDescriptors(context, cb, DS_GENERATE_IMPORTANCE_MAP, constants);
			computeDispatch(context, cb, CS_GENERATE_IMPORTANCE_MAP, dispatchWidth, dispatchHeight, 1);
			computeBarrier(cb);
			setupDescriptors(context, cb, DS_POSTPROCESS_IMPORTANCE_MAP_A, constants);
			computeDispatch(context, cb, CS_POSTPROCESS_IMPORTANCE_MAP_A, dispatchWidth, dispatchHeight, 1);
			computeBarrier(cb);
			setupDescriptors(context, cb, DS_POSTPROCESS_IMPORTANCE_MAP_B, constants);
			computeDispatch(context, cb, CS_POSTPROCESS_IMPORTANCE_MAP_B, dispatchWidth, dispatchHeight, 1);

			cb.end_region();
		}

		cb.end_region();

		computeBarrier(cb);
	}

	// main ssao generation
	{
		cb.begin_region("Generate SSAO");

		auto generateCS = (ComputeShaderID)(CS_GENERATE_Q0 + FFX_CACAO_MAX(0, context->settings.qualityLevel - 1));

		uint32_t dispatchWidth, dispatchHeight, dispatchDepth;

		switch (context->settings.qualityLevel)
		{
		default:
		case FFX_CACAO_QUALITY_LOWEST:
		case FFX_CACAO_QUALITY_LOW:
		case FFX_CACAO_QUALITY_MEDIUM:
			dispatchWidth = dispatchSize(FFX_CACAO_GENERATE_SPARSE_WIDTH, bsi->ssaoBufferWidth);
			dispatchWidth = (dispatchWidth + 4) / 5;
			dispatchHeight = dispatchSize(FFX_CACAO_GENERATE_SPARSE_HEIGHT, bsi->ssaoBufferHeight);
			dispatchDepth = 5;
			break;
		case FFX_CACAO_QUALITY_HIGH:
		case FFX_CACAO_QUALITY_HIGHEST:
			dispatchWidth = dispatchSize(FFX_CACAO_GENERATE_WIDTH, bsi->ssaoBufferWidth);
			dispatchHeight = dispatchSize(FFX_CACAO_GENERATE_HEIGHT, bsi->ssaoBufferHeight);
			dispatchDepth = 1;
			break;
		}

		for (int pass = 0; pass < 4; ++pass)
		{
			if (context->settings.qualityLevel == FFX_CACAO_QUALITY_LOWEST && (pass == 1 || pass == 2))
				continue;

			DescriptorSetID descriptorSetID = context->settings.qualityLevel == FFX_CACAO_QUALITY_HIGHEST ? DS_GENERATE_ADAPTIVE_0 : DS_GENERATE_0;
			descriptorSetID = DescriptorSetID(descriptorSetID + pass);

			setupDescriptors(context, cb, descriptorSetID, constants);
			computeDispatch(context, cb, generateCS, dispatchWidth, dispatchHeight, dispatchDepth);
		}

		cb.end_region();
	}

	uint32_t blurPassCount = context->settings.blurPassCount;
	blurPassCount = FFX_CACAO_CLAMP(blurPassCount, 0, MAX_BLUR_PASSES);

	// de-interleaved blur
	if (blurPassCount)
	{
		computeBarrier(cb);

		cb.begin_region("Deinterleaved Blur");

		uint32_t w = 4 * FFX_CACAO_BLUR_WIDTH - 2 * blurPassCount;
		uint32_t h = 3 * FFX_CACAO_BLUR_HEIGHT - 2 * blurPassCount;
		uint32_t dispatchWidth = dispatchSize(w, bsi->ssaoBufferWidth);
		uint32_t dispatchHeight = dispatchSize(h, bsi->ssaoBufferHeight);

		for (int pass = 0; pass < 4; ++pass)
		{
			if (context->settings.qualityLevel == FFX_CACAO_QUALITY_LOWEST && (pass == 1 || pass == 2))
				continue;

			auto blurShaderID = ComputeShaderID(CS_EDGE_SENSITIVE_BLUR_1 + blurPassCount - 1);
			auto descriptorSetID = DescriptorSetID(DS_EDGE_SENSITIVE_BLUR_0 + pass);
			setupDescriptors(context, cb, descriptorSetID, constants);
			computeDispatch(context, cb, blurShaderID, dispatchWidth, dispatchHeight, 1);
		}

		cb.end_region();

		computeBarrier(cb);
	}
	else
	{
		computeBarrier(cb);
	}

	if (context->useDownsampledSsao)
	{
		cb.begin_region("Bilateral Upsample");

		uint32_t dispatchWidth = dispatchSize(2 * FFX_CACAO_BILATERAL_UPSCALE_WIDTH, bsi->inputOutputBufferWidth);
		uint32_t dispatchHeight = dispatchSize(2 * FFX_CACAO_BILATERAL_UPSCALE_HEIGHT, bsi->inputOutputBufferHeight);

		DescriptorSetID descriptorSetID = blurPassCount ? DS_BILATERAL_UPSAMPLE_PONG : DS_BILATERAL_UPSAMPLE_PING;
		ComputeShaderID upscaler;
		switch (context->settings.qualityLevel)
		{
		default:
		case FFX_CACAO_QUALITY_LOWEST:
			upscaler = CS_UPSCALE_BILATERAL_5X5_HALF;
			break;
		case FFX_CACAO_QUALITY_LOW:
		case FFX_CACAO_QUALITY_MEDIUM:
			upscaler = CS_UPSCALE_BILATERAL_5X5_NON_SMART;
			break;
		case FFX_CACAO_QUALITY_HIGH:
		case FFX_CACAO_QUALITY_HIGHEST:
			upscaler = CS_UPSCALE_BILATERAL_5X5_SMART;
			break;
		}

		setupDescriptors(context, cb, descriptorSetID, constants);
		computeDispatch(context, cb, upscaler, dispatchWidth, dispatchHeight, 1);

		cb.end_region();
	}
	else
	{
		cb.begin_region("Reinterleave");

		uint32_t dispatchWidth = dispatchSize(FFX_CACAO_APPLY_WIDTH, bsi->inputOutputBufferWidth);
		uint32_t dispatchHeight = dispatchSize(FFX_CACAO_APPLY_HEIGHT, bsi->inputOutputBufferHeight);

		DescriptorSetID descriptorSetID = blurPassCount ? DS_APPLY_PONG : DS_APPLY_PING;
		setupDescriptors(context, cb, descriptorSetID, constants);

		switch (context->settings.qualityLevel)
		{
		case FFX_CACAO_QUALITY_LOWEST:
			computeDispatch(context, cb, CS_NON_SMART_HALF_APPLY, dispatchWidth, dispatchHeight, 1);
			break;
		case FFX_CACAO_QUALITY_LOW:
			computeDispatch(context, cb, CS_NON_SMART_APPLY, dispatchWidth, dispatchHeight, 1);
			break;
		default:
			computeDispatch(context, cb, CS_APPLY, dispatchWidth, dispatchHeight, 1);
			break;
		}

		cb.end_region();
	}

	cb.end_region();

	// End of render pass barrier takes care of rest.

	return FFX_CACAO_STATUS_OK;
}

#ifdef __cplusplus
}
#endif
