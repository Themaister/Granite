#pragma once

#include "wsi.hpp"
#include "render_context.hpp"
#include "scene_loader.hpp"
#include "animation_system.hpp"
#include "renderer.hpp"

namespace Granite
{
class Application
{
public:
	Application(unsigned width, unsigned height);
	virtual ~Application() = default;
	virtual void render_frame(double frame_time, double elapsed_time) = 0;

	Vulkan::WSI &get_wsi()
	{
		return wsi;
	}

private:
	Vulkan::WSI wsi;
};

class SceneViewerApplication : public Application
{
public:
	SceneViewerApplication(const std::string &path, unsigned width, unsigned height);
	void render_frame(double frame_time, double elapsed_time) override;

private:
	RenderContext context;
	Renderer renderer;
	FPSCamera cam;
	VisibilityList visible;
	SceneLoader scene_loader;
	std::unique_ptr<AnimationSystem> animation_system;
};

extern int application_main(int argc, char *argv[]);
int mainloop_run(Application &app);
}