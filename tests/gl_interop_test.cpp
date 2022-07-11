#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "device.hpp"
#include "context.hpp"
#include "global_managers_init.hpp"
#include <stdlib.h>
#include <cmath>

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

int main()
{
	Granite::Global::init(1, Granite::Global::MANAGER_FEATURE_DEFAULT_BITS);
	if (!glfwInit())
		return EXIT_FAILURE;

	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);

	GLFWwindow *window = glfwCreateWindow(1280, 720, "GL interop", nullptr, nullptr);
	if (!window)
	{
		LOGE("Failed to create window.\n");
		return EXIT_FAILURE;
	}

	glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
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

	glfwSwapInterval(1);
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

	GLint gltrue = GL_TRUE;
	glMemoryObjectParameterivEXT(glmem, GL_DEDICATED_MEMORY_OBJECT_EXT, &gltrue);

	const char *vendor = (const char *)glGetString(GL_VENDOR);
	LOGI("GL vendor: %s\n", vendor);

	check_gl_error();

#ifdef _WIN32
	GLubyte luid[GL_LUID_SIZE_EXT] = {};
	auto &features = device.get_device_features();
	glGetUnsignedBytevEXT(GL_DEVICE_LUID_EXT, luid);

	check_gl_error();

	for (unsigned i = 0; i < VK_LUID_SIZE; i++)
	{
		if (features.id_properties.deviceLUID[i] != luid[i])
		{
			LOGE("LUID mismatch.\n");
			return EXIT_FAILURE;
		}
	}

	glImportMemoryWin32HandleEXT(glmem, image->get_allocation().get_size(),
	                             GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, exported_image.handle);
	check_gl_error();
	CloseHandle(exported_image.handle);
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

	check_gl_error();

	// We'll blit the result to screen with BlitFramebuffer.
	glNamedFramebufferTexture(glfbo, GL_COLOR_ATTACHMENT0, gltex, 0);

	GLenum status;
	if ((status = glCheckNamedFramebufferStatus(glfbo, GL_READ_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE)
	{
		LOGE("Failed to bind framebuffer (#%x).\n", status);
		check_gl_error();
		return EXIT_FAILURE;
	}

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

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
			auto ext_semaphore = device.request_binary_semaphore_external();
			device.submit_empty(CommandBuffer::Type::Generic, nullptr, ext_semaphore.get());
			auto exported_semaphore =
			    ext_semaphore->export_to_handle(ExternalHandle::get_opaque_semaphore_handle_type());

			glGenSemaphoresEXT(1, &glsem);

#ifdef _WIN32
			glImportSemaphoreWin32HandleEXT(glsem, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, exported_semaphore.handle);
			check_gl_error();
			CloseHandle(exported_semaphore.handle);
#else
			// Importing an FD takes ownership of it. We'll reimport the FD, so need to dup it.
			glImportSemaphoreFdEXT(glsem, GL_HANDLE_TYPE_OPAQUE_FD_EXT, exported_semaphore.handle);
#endif

			// Wait. The layout matches whatever we used when releasing the image.
			glWaitSemaphoreEXT(glsem, 0, nullptr, 1, &gltex, &gllayout);
			glDeleteSemaphoresEXT(1, &glsem);
			check_gl_error();
		}

		int fb_width, fb_height;
		glfwGetFramebufferSize(window, &fb_width, &fb_height);

		glBlitNamedFramebuffer(glfbo, 0,
		                       0, 0, GLint(image->get_width()), GLint(image->get_height()),
		                       0, 0, fb_width, fb_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		check_gl_error();

		// We're done using the semaphore. Import the layout from GL and wait on it to avoid write-after-read hazard.
		{
			auto ext_semaphore = device.request_binary_semaphore_external();
			// Mark that the semaphore has already been signalled.
			// We'll do a reverse import basically. GL imports the semaphore and signals it.
			ext_semaphore->signal_external();
			auto exported_semaphore =
				ext_semaphore->export_to_handle(ExternalHandle::get_opaque_semaphore_handle_type());

			glGenSemaphoresEXT(1, &glsem);

#ifdef _WIN32
			glImportSemaphoreWin32HandleEXT(glsem, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, exported_semaphore.handle);
			CloseHandle(exported_semaphore.handle);
#else
			// Importing an FD takes ownership of it. We'll reimport the FD, so need to dup it.
			glImportSemaphoreFdEXT(glsem, GL_HANDLE_TYPE_OPAQUE_FD_EXT, exported_semaphore.handle);
#endif

			glSignalSemaphoreEXT(glsem, 0, nullptr, 1, &gltex, &gllayout);
			glDeleteSemaphoresEXT(1, &glsem);
			check_gl_error();

			device.add_wait_semaphore(CommandBuffer::Type::Generic, std::move(ext_semaphore),
			                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, true);
		}

		glfwSwapBuffers(window);
		device.next_frame_context();
		frame_count++;

		check_gl_error();
	}

	glDeleteFramebuffers(1, &glfbo);
	glDeleteTextures(1, &gltex);
	glDeleteMemoryObjectsEXT(1, &glmem);

	check_gl_error();

	glfwDestroyWindow(window);
	glfwTerminate();
}