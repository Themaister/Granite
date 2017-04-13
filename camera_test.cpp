#include "transforms.hpp"
#include "camera.hpp"
#include "util.hpp"

using namespace Granite;

static void log_vec3(const char *tag, const vec3 &v)
{
	LOGI("%s: %.4f, %.4f, %.4f\n", tag, v.x, v.y, v.z);
}

static void log_quat(const char *tag, const quat &q)
{
	LOGI("%s: %.4f, [%.4f, %.4f, %.4f]\n", tag, q.w, q.x, q.y, q.z);
}

int main()
{
	Camera cam;
	cam.look_at(vec3(2.0f), vec3(3.0f));
	log_vec3("front", cam.get_front());
	log_vec3("right", cam.get_right());
	log_vec3("up", cam.get_up());
	log_quat("rot", cam.get_rotation());

	cam.set_aspect(1.5f);
	cam.set_depth_range(1.0f, 1000.0f);
	cam.set_fovy(glm::half_pi<float>());
}