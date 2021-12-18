#include "transforms.hpp"
#include "logging.hpp"
#include "math.hpp"
#include "scene_formats.hpp"
#include "muglm/muglm_impl.hpp"

using namespace Granite;

static void test_rotation()
{
	SceneFormats::AnimationChannel channel;
	channel.type = SceneFormats::AnimationChannel::Type::Rotation;
	channel.timestamps = { 0.0f, 1.0f, 2.0f, 3.0f };
	channel.spherical.values = {
		angleAxis(0.1f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(0.4f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(0.9f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(1.6f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
	};

	channel = channel.build_smooth_rail_animation(0.0f);

	for (int i = 0; i <= 10; i++)
	{
		float ft = float(i) / 10.0f;
		float t = 1.0f + ft;
		vec4 q = channel.spherical.sample_squad(1, ft).as_vec4();
		LOGI("t = %f [theta = %f] [expected = %f] [%f %f %f]\n",
		     t, 2.0f * acos(q.w), 0.1f * (t + 1.0f) * (t + 1.0f), q.x, q.y, q.z);
	}
}

static void test_translation()
{
	SceneFormats::AnimationChannel channel;
	channel.timestamps = { 0.0f, 1.0f, 2.0f, 3.0f };

	channel.positional.values = {
		vec3(1.0f, 0.0f, 0.0f),
		vec3(4.0f, 0.0f, 0.0f),
		vec3(9.0f, 0.0f, 0.0f),
		vec3(16.0f, 0.0f, 0.0f),
	};
	channel.type = SceneFormats::AnimationChannel::Type::Translation;
	channel = channel.build_smooth_rail_animation(0.0f);
	for (int i = 0; i <= 10; i++)
	{
		float ft = float(i) / 10.0f;
		float p = channel.positional.sample_spline(1, ft, 1.0f).x;
		LOGI("x = %f (%f)\n", p, (2.0f + ft) * (2.0f + ft));
	}
}

int main()
{
	test_rotation();
	test_translation();
}