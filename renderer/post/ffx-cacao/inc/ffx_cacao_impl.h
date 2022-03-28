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

/*! \file */

#pragma once

#include "ffx_cacao.h"

#include "device.hpp"
#include "command_buffer.hpp"
#include "image.hpp"
#include "buffer.hpp"

/**
	The return codes for the API functions.
*/
typedef enum FFX_CACAO_Status {
	FFX_CACAO_STATUS_OK               =  0,
	FFX_CACAO_STATUS_INVALID_ARGUMENT = -1,
	FFX_CACAO_STATUS_INVALID_POINTER  = -2,
	FFX_CACAO_STATUS_OUT_OF_MEMORY    = -3,
	FFX_CACAO_STATUS_FAILED           = -4,
} FFX_CACAO_Status;

typedef struct FFX_CACAO_GraniteContext FFX_CACAO_GraniteContext;

struct FFX_CACAO_GraniteCreateInfo
{
	Vulkan::Device *device;
};

struct FFX_CACAO_GraniteScreenSizeInfo
{
	uint32_t width;
	uint32_t height;
	const Vulkan::ImageView *depthView;
	const Vulkan::ImageView *normalsView;
	const Vulkan::ImageView *outputView;
	FFX_CACAO_Bool useDownsampledSsao;
};

#ifdef __cplusplus
extern "C"
{
#endif

FFX_CACAO_Status FFX_CACAO_GraniteAllocContext(FFX_CACAO_GraniteContext** context, const FFX_CACAO_GraniteCreateInfo *info);
FFX_CACAO_Status FFX_CACAO_GraniteDestroyContext(FFX_CACAO_GraniteContext* context);
FFX_CACAO_Status FFX_CACAO_GraniteInitScreenSizeDependentResources(FFX_CACAO_GraniteContext* context, const FFX_CACAO_GraniteScreenSizeInfo* info);
FFX_CACAO_Status FFX_CACAO_GraniteDestroyScreenSizeDependentResources(FFX_CACAO_GraniteContext* context);
FFX_CACAO_Status FFX_CACAO_GraniteUpdateSettings(FFX_CACAO_GraniteContext* context, const FFX_CACAO_Settings* settings);
FFX_CACAO_Status FFX_CACAO_GraniteDraw(FFX_CACAO_GraniteContext* context, Vulkan::CommandBuffer &commandList,
                                       const FFX_CACAO_Matrix4x4* proj, const FFX_CACAO_Matrix4x4* normalsToView);

#ifdef __cplusplus
}
#endif
