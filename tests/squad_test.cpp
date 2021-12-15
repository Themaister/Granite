#include "transforms.hpp"
#include "logging.hpp"
#include "math.hpp"
#include "scene_formats.hpp"
#include "muglm/muglm_impl.hpp"

using namespace Granite;

int main()
{
	SceneFormats::AnimationChannel channel;
	channel.type = SceneFormats::AnimationChannel::Type::ImplicitSquadRotation;
	channel.timestamps = { 0.0f, 1.0f, 2.0f, 3.0f };
	channel.spherical.values = {
		angleAxis(0.0f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(0.1f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(0.2f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(0.3f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
	};

	for (int i = 0; i <= 10; i++)
	{
		float ft = float(i) / 10.0f;
		float t = 1.0f + ft;
		vec4 q = channel.spherical.sample_squad(1, ft, 1.0f).as_vec4();
		LOGI("t = %f [theta = %f] [%f %f %f]\n", t, 2.0f * acos(q.w), q.x, q.y, q.z);
	}
}