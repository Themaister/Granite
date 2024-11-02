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

#pragma once

#include "device.hpp"
#include "image.hpp"
#include "d3d12.h"
#include "dxgi1_6.h"
#include "small_vector.hpp"
#include "dynamic_library.hpp"
#include <wrl.h>

namespace Vulkan
{
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

class DXGIInteropSwapchain
{
public:
	bool init_interop_device(Device &device);
	~DXGIInteropSwapchain();

	bool init_swapchain(HWND hwnd, VkSurfaceFormatKHR format, unsigned width, unsigned height, unsigned count);
	VkImage get_vulkan_image() const;
	VkSurfaceFormatKHR get_current_surface_format() const;

	bool acquire(Semaphore &acquire_semaphore);
	bool present(Semaphore release_semaphore, bool vsync);
	bool wait_latency(unsigned latency_frames);

private:
	Device *vk_device = nullptr;
	Util::DynamicLibrary d3d12_lib, dxgi_lib;
	HWND hwnd = nullptr;
	HANDLE latency_handle = nullptr;
	ComPtr<ID3D12Device4> device;
	ComPtr<ID3D12CommandQueue> queue;
	ComPtr<IDXGIFactory5> dxgi_factory;
	ComPtr<IDXGISwapChain3> swapchain;
	ComPtr<ID3D12GraphicsCommandList> list;
	ComPtr<ID3D12Fence> fence;
	Semaphore vk_fence;
	uint64_t fence_value = 0;
	VkSurfaceFormatKHR surface_format = {};
	bool allow_tearing = false;

	struct PerFrameState
	{
		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12Resource> backbuffer;
		uint64_t wait_fence_value = 0;
	};
	Util::SmallVector<PerFrameState> backbuffers;
	ComPtr<ID3D12Resource> blit_backbuffer;
	ImageHandle vulkan_backbuffer;

	bool setup_per_frame_state(PerFrameState &state, unsigned index);
	void reset_backbuffer_state();

	uint64_t completed_presents = 0;
	uint64_t completed_waits = 0;
};
}
