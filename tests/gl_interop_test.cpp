#include "glad/glad.h"
#include "device.hpp"
#include "context.hpp"
#include "global_managers_init.hpp"
#include <stdlib.h>
#include <cmath>

#include <SDL3/SDL.h>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace Vulkan;

static void check_gl_error()
{
	GLenum err;
	if ((err = glGetError()) != GL_NO_ERROR)
	{
		LOGE("GL error: #%x.\n", err);
		exit(EXIT_FAILURE);
	}
}

static void import_semaphore(GLuint &glsem, const ExternalHandle &handle)
{
	glGenSemaphoresEXT(1, &glsem);
#ifdef _WIN32
	glImportSemaphoreWin32HandleEXT(glsem, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handle.handle);
	CloseHandle(handle.handle);
#else
	// Importing an FD takes ownership of it.
	glImportSemaphoreFdEXT(glsem, GL_HANDLE_TYPE_OPAQUE_FD_EXT, handle.handle);
#endif
	check_gl_error();
}

int main()
{
	Granite::Global::init(Granite::Global::MANAGER_FEATURE_DEFAULT_BITS, 1);
	if (!SDL_Init(SDL_INIT_VIDEO))
		return EXIT_FAILURE;

	SDL_Window *window = SDL_CreateWindow("GL interop", 1280, 720, SDL_WINDOW_OPENGL);
	if (!window)
	{
		LOGE("Failed to create window.\n");
		return EXIT_FAILURE;
	}

	SDL_GLContext glctx = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, glctx);

	if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
	{
		LOGE("Failed to load GL context functions.\n");
		return EXIT_FAILURE;
	}

	if (!GLAD_GL_EXT_memory_object || !GLAD_GL_EXT_semaphore)
	{
		LOGE("External functions not supported.\n");
		return EXIT_FAILURE;
	}

#ifdef _WIN32
	if (!GLAD_GL_EXT_memory_object_win32 || !GLAD_GL_EXT_semaphore_win32)
	{
		LOGE("External handle functions not supported.\n");
		return EXIT_FAILURE;
	}
#else
	if (!GLAD_GL_EXT_memory_object_fd || !GLAD_GL_EXT_semaphore_fd)
	{
		LOGE("External FD functions not supported.\n");
		return EXIT_FAILURE;
	}
#endif

	SDL_GL_SetSwapInterval(1);
	unsigned frame_count = 0;

	Context ctx;
	Device device;

	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;

	Context::SystemHandles handles = {};
	handles.filesystem = GRANITE_FILESYSTEM();
	ctx.set_system_handles(handles);
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
	{
		LOGE("Failed to create Vulkan device.\n");
		return EXIT_FAILURE;
	}

	device.set_context(ctx);

	if (!device.get_device_features().supports_external)
	{
		LOGE("Vulkan device does not support external.\n");
		return EXIT_FAILURE;
	}

	auto image_info = ImageCreateInfo::render_target(512, 512, VK_FORMAT_R8G8B8A8_UNORM);
	image_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	image_info.misc = IMAGE_MISC_EXTERNAL_MEMORY_BIT;
	auto image = device.create_image(image_info);

	// Export image.
	auto exported_image = image->export_handle();

	// Import image as a texture.
	GLuint gltex;
	GLuint glmem;
	GLuint glfbo;
	glCreateTextures(GL_TEXTURE_2D, 1, &gltex);
	glCreateMemoryObjectsEXT(1, &glmem);
	glCreateFramebuffers(1, &glfbo);

	// We always use dedicated allocations in Granite for external objects.
	GLint gltrue = GL_TRUE;
	glMemoryObjectParameterivEXT(glmem, GL_DEDICATED_MEMORY_OBJECT_EXT, &gltrue);

	check_gl_error();

	const char *vendor = (const char *)glGetString(GL_VENDOR);
	LOGI("GL vendor: %s\n", vendor);

	auto &features = device.get_device_features();
	if (features.vk11_props.deviceLUIDValid)
	{
		GLubyte luid[GL_LUID_SIZE_EXT] = {};
		glGetUnsignedBytevEXT(GL_DEVICE_LUID_EXT, luid);

		if (memcmp(features.vk11_props.deviceLUID, luid, GL_LUID_SIZE_EXT) != 0)
		{
			LOGE("LUID mismatch.\n");
			return EXIT_FAILURE;
		}
	}

#ifdef _WIN32
	glImportMemoryWin32HandleEXT(glmem, image->get_allocation().get_size(),
	                             GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, exported_image.handle);
#else
	// Importing takes ownership of the FD.
	glImportMemoryFdEXT(glmem, image->get_allocation().get_size(),
	                    GL_HANDLE_TYPE_OPAQUE_FD_EXT, exported_image.handle);
#endif

	check_gl_error();

	glTextureStorageMem2DEXT(gltex, 1, GL_RGBA8,
	                         GLsizei(image->get_width()),
	                         GLsizei(image->get_height()),
	                         glmem, 0);

#ifdef _WIN32
	// The HANDLE seems to be consumed at TextureStorage time, otherwise we get OUT_OF_MEMORY error on NV Windows.
	// Sort of makes sense since it's a dedicated allocation?
	CloseHandle(exported_image.handle);
#endif

	// We'll blit the result to screen with BlitFramebuffer.
	glNamedFramebufferTexture(glfbo, GL_COLOR_ATTACHMENT0, gltex, 0);

	GLenum status;
	if ((status = glCheckNamedFramebufferStatus(glfbo, GL_READ_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE)
	{
		LOGE("Failed to bind framebuffer (#%x).\n", status);
		return EXIT_FAILURE;
	}

	bool alive = true;
	SDL_Event e;
	while (alive)
	{
		while (SDL_PollEvent(&e))
			if (e.type == SDL_EVENT_QUIT)
				alive = false;

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

			cmd->end_render_pass();
			cmd->release_external_image_barrier(
			    *image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
			device.submit(cmd);
		}

		// Import the FD.
		const GLenum gllayout = GL_LAYOUT_COLOR_ATTACHMENT_EXT;
		GLuint glsem;

		{
			// Synchronize with OpenGL. Export a handle.
			auto ext_semaphore = device.request_semaphore_external(
			    VK_SEMAPHORE_TYPE_BINARY, ExternalHandle::get_opaque_semaphore_handle_type());
			device.submit_empty(CommandBuffer::Type::Generic, nullptr, ext_semaphore.get());
			auto exported_semaphore = ext_semaphore->export_to_handle();

			import_semaphore(glsem, exported_semaphore);

			// Wait. The layout matches whatever we used when releasing the image.
			glWaitSemaphoreEXT(glsem, 0, nullptr, 1, &gltex, &gllayout);
			glDeleteSemaphoresEXT(1, &glsem);
		}

		int fb_width, fb_height;
		SDL_GetWindowSize(window, &fb_width, &fb_height);

		glBlitNamedFramebuffer(glfbo, 0,
		                       0, 0, GLint(image->get_width()), GLint(image->get_height()),
		                       0, 0, fb_width, fb_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		// We're done using the semaphore. Import the layout from GL and wait on it to avoid write-after-read hazard.
		// We could keep reusing the semaphore, but NV Linux seems to trigger random ~5 second hangs
		// when doing that for some reason, so just use one semaphore per signal wait pair.
		{
			// Synchronize with OpenGL. Export a handle that GL can signal.
			auto ext_semaphore = device.request_semaphore_external(
			    VK_SEMAPHORE_TYPE_BINARY, ExternalHandle::get_opaque_semaphore_handle_type());
			// Have to mark the semaphore is signalled since we assert on that being the case when exporting a semaphore.
			ext_semaphore->signal_external();
			auto exported_semaphore = ext_semaphore->export_to_handle();

			import_semaphore(glsem, exported_semaphore);

			glSignalSemaphoreEXT(glsem, 0, nullptr, 1, &gltex, &gllayout);

			// Unsure if we have to flush to make sure the signal has been processed.
			glFlush();

			device.add_wait_semaphore(CommandBuffer::Type::Generic, std::move(ext_semaphore),
			                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, true);

			glDeleteSemaphoresEXT(1, &glsem);
		}

		SDL_GL_SwapWindow(window);
		device.next_frame_context();
		frame_count++;
	}

	glDeleteFramebuffers(1, &glfbo);
	glDeleteTextures(1, &gltex);
	glDeleteMemoryObjectsEXT(1, &glmem);

	check_gl_error();

	SDL_GL_DeleteContext(glctx);
	SDL_DestroyWindow(window);
	SDL_Quit();
}
