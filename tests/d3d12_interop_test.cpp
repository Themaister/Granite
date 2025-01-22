#include "device.hpp"
#include "context.hpp"
#include "global_managers_init.hpp"
#include <cmath>

#include "dxgi1_6.h"
#include "d3d12.h"

#include <SDL3/SDL.h>

using namespace Vulkan;

struct DXGIContext
{
	IDXGIFactory *factory;
	IDXGIAdapter *adapter;
};

static DXGIContext query_adapter()
{
	IDXGIFactory *factory;
	auto hr = CreateDXGIFactory(IID_PPV_ARGS(&factory));

	if (FAILED(hr))
	{
		LOGE("Failed to create DXGI factory.\n");
		return {};
	}

	IDXGIAdapter *adapter = nullptr;
	for (unsigned i = 0; !adapter; i++)
	{
		hr = factory->EnumAdapters(i, &adapter);
		if (hr == DXGI_ERROR_NOT_FOUND)
			break;

		IDXGIAdapter1 *adapter1;
		if (FAILED(adapter->QueryInterface(&adapter1)))
		{
			adapter->Release();
			adapter = nullptr;
			continue;
		}

		DXGI_ADAPTER_DESC1 adapter_desc;
		adapter1->GetDesc1(&adapter_desc);
		if ((adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
		{
			adapter->Release();
			adapter = nullptr;
		}

		adapter1->Release();
	}

	return { factory, adapter };
}

struct D3DContext
{
	ID3D12Device *dev;
	ID3D12CommandQueue *queue;
	ID3D12CommandAllocator *allocator[2];
	ID3D12GraphicsCommandList *list[2];
	DXGIContext dxgi;
	LUID luid;

	ID3D12Resource *back_buffers[2];
	uint64_t wait_timeline[2];
	IDXGISwapChain3 *swapchain;

	ID3D12Resource *texture;
	ID3D12Fence *fence;
};

static D3DContext create_d3d12_device()
{
	auto dxgi_context = query_adapter();
	if (!dxgi_context.adapter)
		return {};

	D3DContext ctx = {};

	auto hr = D3D12CreateDevice(dxgi_context.adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&ctx.dev));

	if (FAILED(hr))
		return {};

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(ctx.dev->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&ctx.queue))))
		return {};

	for (unsigned i = 0; i < 2; i++)
	{
		if (FAILED(ctx.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ctx.allocator[i]))))
			return {};
		if (FAILED(ctx.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		                                      ctx.allocator[i], nullptr,
		                                      IID_PPV_ARGS(&ctx.list[i]))))
			return {};
		ctx.list[i]->Close();
	}

	DXGI_ADAPTER_DESC desc;
	dxgi_context.adapter->GetDesc(&desc);
	ctx.luid = desc.AdapterLuid;

	if (FAILED(hr))
	{
		LOGE("Failed to create D3D12 device.\n");
		return {};
	}

	ctx.dxgi = dxgi_context;
	return ctx;
}

static bool init_swapchain(SDL_Window *window, D3DContext &ctx)
{
	SDL_PropertiesID props = SDL_GetWindowProperties(window);
	SDL_LockProperties(props);
	HWND hwnd = static_cast<HWND>(SDL_GetProperty(props, "SDL.window.win32.hwnd", nullptr));
	SDL_UnlockProperties(props);

	DXGI_SWAP_CHAIN_DESC desc = {};
	desc.BufferCount = 2;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.OutputWindow = hwnd;
	desc.Windowed = TRUE;
	desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferDesc.Width = 512;
	desc.BufferDesc.Height = 512;
	desc.BufferDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.SampleDesc.Count = 1;

	IDXGISwapChain *swapchain;

	auto hr = ctx.dxgi.factory->CreateSwapChain(ctx.queue, &desc, &swapchain);
	if (FAILED(hr))
	{
		LOGE("Failed to create swapchain.\n");
		return false;
	}

	if (FAILED(swapchain->QueryInterface(&ctx.swapchain)))
		return false;
	swapchain->Release();

	for (unsigned i = 0; i < 2; i++)
	{
		if (FAILED(ctx.swapchain->GetBuffer(i, IID_PPV_ARGS(&ctx.back_buffers[i]))))
			return false;
	}

	return true;
}

int main()
{
	if (!SDL_Init(SDL_INIT_VIDEO))
		return EXIT_FAILURE;

	Granite::Global::init(Granite::Global::MANAGER_FEATURE_DEFAULT_BITS, 1);

	auto ctx = create_d3d12_device();
	if (!ctx.dev)
		return EXIT_FAILURE;

	SDL_Window *window = SDL_CreateWindow("D3D12 interop", 1280, 720, 0);
	if (!window)
	{
		LOGE("Failed to create window.\n");
		return EXIT_FAILURE;
	}

	if (!init_swapchain(window, ctx))
		return EXIT_FAILURE;

	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;

	Context vk;
	Device device;
	Context::SystemHandles handles = {};
	handles.filesystem = GRANITE_FILESYSTEM();
	vk.set_system_handles(handles);
	if (!vk.init_instance_and_device(nullptr, 0, nullptr, 0))
	{
		LOGE("Failed to create Vulkan device.\n");
		return EXIT_FAILURE;
	}

	device.set_context(vk);

	if (!device.get_device_features().supports_external)
	{
		LOGE("Vulkan device does not support external.\n");
		return EXIT_FAILURE;
	}

	if (memcmp(device.get_device_features().vk11_props.deviceLUID,
	           &ctx.luid, VK_LUID_SIZE) != 0)
	{
		LOGE("LUID mismatch.\n");
		return EXIT_FAILURE;
	}

	// AMD Windows Vulkan only supports importing D3D timeline it seems.
	// Also, it only supports importing D3D11 texture :(
	// So, we get to test this path now :)
	D3D12_RESOURCE_DESC desc = {};
	desc.SampleDesc.Count = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = 512;
	desc.Height = 512;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	desc.MipLevels = 1;
	desc.DepthOrArraySize = 1;
	desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	D3D12_HEAP_PROPERTIES heap_props = {};
	heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
	if (FAILED(ctx.dev->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_SHARED,
	                                            &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&ctx.texture))))
	{
		LOGE("Failed to create texture.\n");
		return EXIT_FAILURE;
	}

	ExternalHandle imported_image;
	if (FAILED(ctx.dev->CreateSharedHandle(ctx.texture, nullptr, GENERIC_ALL, nullptr, &imported_image.handle)))
		return EXIT_FAILURE;
	imported_image.memory_handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;

	auto image_info = ImageCreateInfo::render_target(512, 512, VK_FORMAT_R8G8B8A8_UNORM);
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	image_info.misc = IMAGE_MISC_EXTERNAL_MEMORY_BIT;
	image_info.external = imported_image;
	auto image = device.create_image(image_info);

	if (!image)
	{
		LOGE("Failed to create image.\n");
		return EXIT_FAILURE;
	}

	if (FAILED(ctx.dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&ctx.fence))))
	{
		LOGE("Failed to create fence.\n");
		return EXIT_FAILURE;
	}

	auto timeline = device.request_semaphore_external(VK_SEMAPHORE_TYPE_TIMELINE,
	                                                  VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT);
	if (!timeline)
	{
		LOGE("Failed to create timeline.\n");
		return EXIT_FAILURE;
	}

	ExternalHandle fence_handle;
	fence_handle.semaphore_handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
	if (FAILED(ctx.dev->CreateSharedHandle(ctx.fence, nullptr, GENERIC_ALL, nullptr, &fence_handle.handle)))
	{
		LOGE("Failed to create shared fence handle.\n");
		return EXIT_FAILURE;
	}

	if (!timeline->import_from_handle(fence_handle))
	{
		LOGE("Failed to import timeline.\n");
		return EXIT_FAILURE;
	}

	uint64_t timeline_value = 0;
	unsigned frame_count = 0;
	unsigned wait_context;

	bool alive = true;
	SDL_Event e;
	while (alive)
	{
		while (SDL_PollEvent(&e))
			if (e.type == SDL_EVENT_QUIT)
				alive = false;

		wait_context = frame_count % 2;

		// Render frame in Vulkan
		{
			auto cmd = device.request_command_buffer();
			RenderPassInfo rp_info;
			rp_info.num_color_attachments = 1;
			rp_info.color_attachments[0] = &image->get_view();
			rp_info.store_attachments = 1u << 0;
			rp_info.clear_attachments = 1u << 0;
			rp_info.clear_color[0].float32[0] = float(0.5f + 0.3f * sin(double(frame_count) * 0.010));
			rp_info.clear_color[0].float32[1] = float(0.5f + 0.3f * sin(double(frame_count) * 0.020));
			rp_info.clear_color[0].float32[2] = float(0.5f + 0.3f * sin(double(frame_count) * 0.015));

			// Don't need to reacquire from external queue family if we don't care about the contents being preserved.
			cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

			cmd->begin_render_pass(rp_info);

			VkClearRect clear_rect = {};
			VkClearValue clear_value = {};

			clear_rect.layerCount = 1;
			clear_rect.rect.extent = { 32, 32 };

			for (unsigned i = 0; i < 4; i++)
				clear_value.color.float32[i] = 1.0f - rp_info.clear_color[0].float32[i];

			for (unsigned i = 0; i < 40 * 5; i += 40)
			{
				clear_rect.rect.offset.x = int(256.0 - 16.0 + 100.0 * cos(double(frame_count + i) * 0.02));
				clear_rect.rect.offset.y = int(256.0 - 16.0 + 100.0 * sin(double(frame_count + i) * 0.02));
				cmd->clear_quad(0, clear_rect, clear_value, VK_IMAGE_ASPECT_COLOR_BIT);
			}

			// For non-Vulkan compatible APIs we have to use GENERAL layout.
			// GL external objects understands Vulkan layouts, but D3D obviously does not.
			cmd->end_render_pass();
			cmd->release_external_image_barrier(
			    *image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
			    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
			device.submit(cmd);
		}

		// Signal ID3D12Fence and wait on it in D3D12.
		{
			timeline_value++;
			auto signal = device.request_timeline_semaphore_as_binary(*timeline, timeline_value);
			device.submit_empty(CommandBuffer::Type::Generic, nullptr, signal.get());
			ctx.queue->Wait(ctx.fence, timeline_value);
		}

		auto *allocator = ctx.allocator[wait_context];
		auto *list = ctx.list[wait_context];
		ctx.fence->SetEventOnCompletion(ctx.wait_timeline[wait_context], nullptr);
		allocator->Reset();
		list->Reset(allocator, nullptr);

		unsigned swap_index = ctx.swapchain->GetCurrentBackBufferIndex();

		// Blit shared texture to back buffer.
		{
			D3D12_BOX box = {};
			box.back = 1;
			box.right = 512;
			box.bottom = 512;

			D3D12_TEXTURE_COPY_LOCATION dst = {};
			D3D12_TEXTURE_COPY_LOCATION src = {};
			dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst.pResource = ctx.back_buffers[swap_index];
			src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			src.pResource = ctx.texture;
			list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
			list->Close();

			ID3D12CommandList *submit_list = list;
			ctx.queue->ExecuteCommandLists(1, &submit_list);
		}

		// Release the texture to Vulkan.
		{
			timeline_value++;
			ctx.queue->Signal(ctx.fence, timeline_value);
			ctx.wait_timeline[wait_context] = timeline_value;

			auto waiter = device.request_timeline_semaphore_as_binary(*timeline, timeline_value);
			waiter->signal_external();
			device.add_wait_semaphore(CommandBuffer::Type::Generic, std::move(waiter),
			                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, true);
		}

		ctx.swapchain->Present(1, 0);
		device.next_frame_context();
		frame_count++;
	}

	ctx.fence->SetEventOnCompletion(timeline_value, nullptr);

	for (unsigned i = 0; i < 2; i++)
	{
		ctx.back_buffers[i]->Release();
		ctx.allocator[i]->Release();
		ctx.list[i]->Release();
	}
	ctx.swapchain->Release();
	ctx.dxgi.adapter->Release();
	ctx.dxgi.factory->Release();
	ctx.texture->Release();
	ctx.fence->Release();
	ctx.queue->Release();
	UINT ref = ctx.dev->Release();
	if (ref != 0)
		LOGE("Missed a release on device.\n");

	SDL_DestroyWindow(window);
	SDL_Quit();
}
