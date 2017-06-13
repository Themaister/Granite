#include "application.hpp"

namespace Granite
{
int application_main(int, char **)
{
	SceneViewerApplication app("assets://gltf-sandbox/scenes/test.json", 1280, 720);
	return app.run();
}
}
