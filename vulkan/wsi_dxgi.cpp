/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "wsi_dxgi.hpp"
#include <windows.h>

namespace Vulkan
{
DXGIInteropSwapchain::~DXGIInteropSwapchain()
{
	// Wait-for-idle before teardown.
	if (fence)
		fence->SetEventOnCompletion(fence_value, nullptr);
	if (latency_handle)
		CloseHandle(latency_handle);
}

static bool is_running_on_wine()
{
	// If we're running in Wine for whatever reason, interop like this is completely useless.
	HMODULE ntdll = GetModuleHandleA("ntdll.dll");
	return !ntdll || GetProcAddress(ntdll, "wine_get_version");
}

static bool is_running_in_tool(Device &device)
{
	auto &ext = device.get_device_features();
	if (ext.supports_tooling_info && vkGetPhysicalDeviceToolPropertiesEXT)
	{
		auto gpu = device.get_physical_device();
		uint32_t count = 0;
		vkGetPhysicalDeviceToolPropertiesEXT(gpu, &count, nullptr);
		Util::SmallVector<VkPhysicalDeviceToolPropertiesEXT> tool_props(count);
		for (auto &t : tool_props)
			t = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT };
		vkGetPhysicalDeviceToolPropertiesEXT(gpu, &count, tool_props.data());

		// It's okay for validation to not force this path. We're mostly concerned with RenderDoc, RGP and Nsight.
		for (auto &t : tool_props)
			if (t.purposes & (VK_TOOL_PURPOSE_PROFILING_BIT | VK_TOOL_PURPOSE_TRACING_BIT))
				return true;
	}

	return false;
}

bool DXGIInteropSwapchain::init_interop_device(Device &vk_device_)
{
	vk_device = &vk_device_;

	// If we're running in Wine for whatever reason, interop like this is more harmful than good.
	if (is_running_on_wine())
		return false;

	// If we're running in some capture tool, we need to use Vulkan WSI to avoid confusing it.
	if (is_running_in_tool(*vk_device))
		return false;

	if (!vk_device->get_device_features().vk11_props.deviceLUIDValid)
		return false;

	d3d12_lib = Util::DynamicLibrary("d3d12.dll");
	dxgi_lib = Util::DynamicLibrary("dxgi.dll");

	if (!d3d12_lib)
	{
		LOGE("Failed to find d3d12.dll. Ignoring interop device.\n");
		return false;
	}

	if (!dxgi_lib)
	{
		LOGE("Failed to find dxgi.dll. Ignoring interop device.\n");
		return false;
	}

	auto pfn_CreateDXGIFactory1 =
		dxgi_lib.get_symbol<decltype(&CreateDXGIFactory1)>("CreateDXGIFactory1");
	auto pfn_D3D12CreateDevice =
	    d3d12_lib.get_symbol<decltype(&D3D12CreateDevice)>("D3D12CreateDevice");

	if (!pfn_CreateDXGIFactory1 || !pfn_D3D12CreateDevice)
	{
		LOGE("Failed to find entry points.\n");
		return false;
	}

	HRESULT hr;
	if (FAILED(hr = pfn_CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory))))
	{
		LOGE("Failed to create DXGI factory, hr #%x.\n", unsigned(hr));
		return false;
	}

	LUID luid = {};
	ComPtr<IDXGIAdapter> adapter;
	memcpy(&luid, vk_device->get_device_features().vk11_props.deviceLUID, VK_LUID_SIZE);
	if (FAILED(hr = dxgi_factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
	{
		LOGE("Failed to enumerate DXGI adapter by LUID.\n");
		return false;
	}

	if (FAILED(hr = pfn_D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
	{
		LOGE("Failed to create D3D12Device, hr #%x.\n", unsigned(hr));
		return false;
	}

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
	{
		LOGE("Failed to create command queue, hr #%x.\n", unsigned(hr));
		return false;
	}

	if (FAILED(hr = device->CreateCommandList1(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&list))))
	{
		LOGE("Failed to create command list, hr #%x.\n", unsigned(hr));
		return false;
	}

	if (FAILED(hr = device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence))))
	{
		LOGE("Failed to create shared fence, hr #%x.\n", unsigned(hr));
		return false;
	}

	// Import D3D12 timeline into Vulkan.
	// Other way around is not as well-supported.
	vk_fence = vk_device->request_semaphore_external(
		VK_SEMAPHORE_TYPE_TIMELINE, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT);
	if (!vk_fence)
	{
		LOGE("Failed to create timeline.\n");
		return EXIT_FAILURE;
	}

	ExternalHandle fence_handle;
	fence_handle.semaphore_handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
	if (FAILED(device->CreateSharedHandle(fence.Get(), nullptr,
	                                      GENERIC_ALL, nullptr, &fence_handle.handle)))
	{
		LOGE("Failed to create shared fence handle.\n");
		return EXIT_FAILURE;
	}

	if (!vk_fence->import_from_handle(fence_handle))
	{
		LOGE("Failed to import timeline.\n");
		CloseHandle(fence_handle.handle);
		return false;
	}

	return true;
}

VkImage DXGIInteropSwapchain::get_vulkan_image() const
{
	return vulkan_backbuffer->get_image();
}

static DXGI_FORMAT convert_vk_format(VkFormat fmt)
{
	switch (fmt)
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
		// D3D12 fails to create SRGB swapchain for some reason.
		// We'll import the memory as sRGB however, and it works fine ...
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return DXGI_FORMAT_R10G10B10A2_UNORM;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}

static DXGI_COLOR_SPACE_TYPE convert_vk_color_space(VkColorSpaceKHR colspace)
{
	switch (colspace)
	{
	case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
		return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
	case VK_COLOR_SPACE_HDR10_ST2084_EXT:
		return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
	case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
		return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	default:
		return DXGI_COLOR_SPACE_RESERVED;
	}
}

void DXGIInteropSwapchain::reset_backbuffer_state()
{
	for (auto &buf : backbuffers)
		if (fence)
			fence->SetEventOnCompletion(buf.wait_fence_value, nullptr);
	backbuffers.clear();
}

bool DXGIInteropSwapchain::setup_per_frame_state(PerFrameState &state, unsigned index)
{
	HRESULT hr;
	if (FAILED(hr = swapchain->GetBuffer(index, IID_PPV_ARGS(&state.backbuffer))))
	{
		LOGE("Failed to get backbuffer, hr #%x.\n", unsigned(hr));
		return false;
	}

	if (FAILED(hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
	                                               IID_PPV_ARGS(&state.allocator))))
	{
		LOGE("Failed to create command allocator, hr #%x.\n", unsigned(hr));
		return false;
	}

	return true;
}

bool DXGIInteropSwapchain::init_swapchain(HWND hwnd_, VkSurfaceFormatKHR format,
                                          unsigned width, unsigned height, unsigned count)
{
	if (hwnd && hwnd_ != hwnd)
	{
		reset_backbuffer_state();
		swapchain.Reset();
	}

	hwnd = hwnd_;

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.BufferCount = count;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.Format = convert_vk_format(format.format);
	if (!desc.Format)
		return false;

	auto color_space = convert_vk_color_space(format.colorSpace);
	if (color_space == DXGI_COLOR_SPACE_RESERVED)
		return false;

	BOOL allow_tear = FALSE;
	if (SUCCEEDED(dxgi_factory->CheckFeatureSupport(
		DXGI_FEATURE_PRESENT_ALLOW_TEARING,
		&allow_tear, sizeof(allow_tear)) && allow_tear))
	{
		desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		allow_tearing = true;
	}
	desc.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

	ComPtr<IDXGISwapChain1> swap;
	HRESULT hr;

	reset_backbuffer_state();

	// If we already have a swapchain we can just use ResizeBuffers.
	if (!swapchain)
	{
		if (FAILED(hr = dxgi_factory->CreateSwapChainForHwnd(
				queue.Get(), hwnd, &desc, nullptr, nullptr, &swap)))
		{
			LOGE("Failed to create swapchain, hr #%x.\n", unsigned(hr));
			return false;
		}

		completed_presents = 0;
		completed_waits = 0;

		if (FAILED(swap.As(&swapchain)))
		{
			LOGE("Failed to query swapchain interface.\n");
			return false;
		}

		if (latency_handle)
			CloseHandle(latency_handle);
		latency_handle = swapchain->GetFrameLatencyWaitableObject();

		if (!latency_handle)
		{
			LOGE("Failed to query latency handle.\n");
			return false;
		}

		// Drop semaphore to 0 right away to make code less awkward later.
		if (WaitForSingleObject(latency_handle, INFINITE) != WAIT_OBJECT_0)
		{
			LOGE("Failed to wait for latency object.\n");
			return false;
		}
	}
	else
	{
		if (FAILED(hr = swapchain->ResizeBuffers(count, width, height, desc.Format, desc.Flags)))
		{
			LOGE("Failed to resize buffers, hr #%x.\n", unsigned(hr));
			return false;
		}
	}

	if (FAILED(dxgi_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES)))
	{
		LOGE("Failed to make window association.\n");
		return false;
	}

	surface_format = format;

	UINT space_support = 0;
	if (FAILED(swapchain->CheckColorSpaceSupport(color_space, &space_support)) ||
	    ((space_support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0))
	{
		// Fallback to SDR if HDR doesn't pass check.
		if (FAILED(swapchain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &space_support)) ||
		    ((space_support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0))
		{
			return false;
		}

		LOGW("HDR10 not supported by DXGI swapchain, falling back to SDR.\n");
		surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	}

	if (FAILED(swapchain->SetColorSpace1(color_space)))
	{
		LOGE("Failed to set color space.\n");
		return false;
	}

	backbuffers.resize(desc.BufferCount);
	for (unsigned i = 0; i < desc.BufferCount; i++)
		if (!setup_per_frame_state(backbuffers[i], i))
			return false;

	ExternalHandle imported_image;
	imported_image.memory_handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;

	D3D12_RESOURCE_DESC blit_desc = {};
	blit_desc.Width = width;
	blit_desc.Height = height;
	blit_desc.Format = desc.Format;
	blit_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	blit_desc.SampleDesc.Count = 1;
	blit_desc.DepthOrArraySize = 1;
	blit_desc.MipLevels = 1;
	blit_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	blit_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	D3D12_HEAP_PROPERTIES heap_props = {};
	heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

	blit_backbuffer.Reset();
	if (FAILED(hr = device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_SHARED, &blit_desc,
	                                                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&blit_backbuffer))))
	{
		LOGE("Failed to create blit render target, hr #%x.\n", unsigned(hr));
		return false;
	}

	if (FAILED(hr = device->CreateSharedHandle(blit_backbuffer.Get(), nullptr, GENERIC_ALL, nullptr,
	                                           &imported_image.handle)))
	{
		LOGE("Failed to create shared handle, hr #%x.\n", unsigned(hr));
		return false;
	}

	auto image_info = ImageCreateInfo::render_target(width, height, format.format);
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info.misc = IMAGE_MISC_EXTERNAL_MEMORY_BIT;
	image_info.external = imported_image;

	vulkan_backbuffer = vk_device->create_image(image_info);
	if (!vulkan_backbuffer)
	{
		LOGE("Failed to create shared Vulkan image, hr #%x.\n", unsigned(hr));
		return false;
	}
	vulkan_backbuffer->set_swapchain_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	return true;
}

VkSurfaceFormatKHR DXGIInteropSwapchain::get_current_surface_format() const
{
	return surface_format;
}

bool DXGIInteropSwapchain::wait_latency(unsigned latency_frames)
{
	uint64_t target_wait_count = completed_presents - latency_frames;

	if (latency_handle && (target_wait_count & (1ull << 63)) == 0)
	{
		while (completed_waits < target_wait_count)
		{
			if (WaitForSingleObject(latency_handle, INFINITE) != WAIT_OBJECT_0)
			{
				LOGE("Failed to wait for latency object.\n");
				return false;
			}
			completed_waits++;
		}
	}

	return true;
}

bool DXGIInteropSwapchain::acquire(Semaphore &acquire_semaphore)
{
	// AMD workaround. Driver freaks out if trying to wait for D3D12 timeline value of 0.
	queue->Signal(fence.Get(), ++fence_value);

	acquire_semaphore = vk_device->request_timeline_semaphore_as_binary(*vk_fence, fence_value);
	return true;
}

bool DXGIInteropSwapchain::present(Vulkan::Semaphore release_semaphore, bool vsync)
{
	unsigned index = swapchain->GetCurrentBackBufferIndex();
	auto &per_frame = backbuffers[index];

	vk_device->add_wait_semaphore(CommandBuffer::Type::Generic, std::move(release_semaphore),
	                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, true);

	auto cmd = vk_device->request_command_buffer();
	cmd->release_image_barrier(*vulkan_backbuffer,
	                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL,
	                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0);

	vk_device->submit(cmd);
	auto timeline_signal = vk_device->request_timeline_semaphore_as_binary(*vk_fence, ++fence_value);
	vk_device->submit_empty(CommandBuffer::Type::Generic, nullptr, timeline_signal.get());
	queue->Wait(fence.Get(), fence_value);

	fence->SetEventOnCompletion(per_frame.wait_fence_value, nullptr);

	if (FAILED(per_frame.allocator->Reset()))
	{
		LOGE("Failed to reset command allocator.\n");
		return false;
	}

	list->Reset(per_frame.allocator.Get(), nullptr);

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.pResource = per_frame.backbuffer.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	list->ResourceBarrier(1, &barrier);

	D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.pResource = per_frame.backbuffer.Get();
	src.pResource = blit_backbuffer.Get();
	list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	list->ResourceBarrier(1, &barrier);

	if (FAILED(list->Close()))
	{
		LOGE("Failed to close command list.\n");
		return false;
	}

	ID3D12CommandList *cmdlist = list.Get();
	queue->ExecuteCommandLists(1, &cmdlist);
	queue->Signal(fence.Get(), ++fence_value);
	per_frame.wait_fence_value = fence_value;

	HRESULT hr = swapchain->Present(vsync ? 1 : 0, !vsync && allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
	if (FAILED(hr))
	{
		LOGE("Failed to present, hr #%x.\n", unsigned(hr));
		return false;
	}

	completed_presents++;
	return true;
}
}
