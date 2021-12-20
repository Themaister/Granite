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

static void test_odd_timestamp_slerp()
{
	SceneFormats::AnimationChannel channel;
	channel.timestamps = { 0.0f, 1.0f, 1.5f, 1.8f, 2.2f, 5.0f };
	channel.type = SceneFormats::AnimationChannel::Type::Rotation;

	channel.spherical.values = {
		angleAxis(0.1f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(0.4f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(0.9f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(0.1f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(0.2f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
		angleAxis(0.1f, vec3(0.0f, 0.0f, 1.0f)).as_vec4(),
	};

	channel = channel.build_smooth_rail_animation(0.0f);

	constexpr int center = 2200;
	constexpr int stride = 1000;
	for (int i = center - 10; i < center + 10; i++)
	{
		unsigned index;
		float phase;
		float dt;

		float t = float(i + 1) / float(stride);
		channel.get_index_phase(t, index, phase, dt);
		quat v2 = channel.spherical.sample_squad(index, phase);

		t = float(i) / float(stride);
		channel.get_index_phase(t, index, phase, dt);
		quat v1 = channel.spherical.sample_squad(index, phase);

		t = float(i - 1) / float(stride);
		channel.get_index_phase(t, index, phase, dt);
		quat v0 = channel.spherical.sample_squad(index, phase);

		float angle0 = 2.0f * acos(v0.w);
		float angle1 = 2.0f * acos(v1.w);
		float angle2 = 2.0f * acos(v2.w);

		float acc = (angle2 - angle1) - (angle1 - angle0);
		acc *= float(stride) * float(stride);
		float v = angle2 - angle1;
		v *= float(stride);

		LOGI("i = %u, theta = %f, v = %f, a = %f\n", i, angle1, v, acc);
	}
}

static double compute_inner_control_point_delta(double q0, double q1, double q2, double dt0, double dt1)
{
	double delta_k = q2 - q1;
	double delta_k_minus1 = q0 - q1;
	double segment_time = 0.5 * (dt0 + dt1);
	// We sample velocity at the center of the segment when taking the difference.
	// Future sample is at t = +1/2 dt
	// Past sample is at t = -1/2 dt
	double absolute_accel = (delta_k / dt1 + delta_k_minus1 / dt0) * segment_time;
	double delta = 0.25f * dt1 * dt1 * absolute_accel;
	return delta;
}

static double compute_inner_control_point(double q0, double delta)
{
	return q0 - delta;
}

static double eval_squad(const std::vector<double> &timestamps, const std::vector<double> &coeff, double t)
{
	size_t end_i = 1;
	while (t >= timestamps[end_i] && end_i + 1 < timestamps.size())
		end_i++;

	size_t start_i = end_i - 1;
	double local_t = clamp((t - timestamps[start_i]) / (timestamps[end_i] - timestamps[start_i]), 0.0, 1.0);

	double q0 = coeff[3 * start_i + 1];
	double a = coeff[3 * start_i + 2];
	double b = coeff[3 * start_i + 3];
	double q1 = coeff[3 * start_i + 4];

	return mix(mix(q0, q1, local_t), mix(a, b, local_t), 2.0 * local_t * (1.0 - local_t));
}

static void test_squad_spline()
{
	const auto reference_value = [](double t) {
		return 0.5 * t - 0.25 * t * t;
	};

	std::vector<double> values;
	std::vector<double> timestamps;
#if 1
	timestamps = {0, 1.0, 1.8, 2.1, 2.9, 3.0, 4.2, 4.3, 5.0, 6.0};
#else
	timestamps.push_back(0.0);
	timestamps.push_back(0.5);
	timestamps.push_back(1.0);
	timestamps.push_back(1.5);
	timestamps.push_back(2.0);
	timestamps.push_back(2.5);
	timestamps.push_back(3.0);
#endif
	for (auto t : timestamps)
		values.push_back(reference_value(t));

	std::vector<double> dt(timestamps.size());
	for (size_t i = 0; i < dt.size() - 1; i++)
		dt[i] = timestamps[i + 1] - timestamps[i];
	dt[dt.size() - 1] = dt[dt.size() - 2];

	std::vector<double> deltas(timestamps.size());
	deltas[0] = compute_inner_control_point_delta(values[0], values[1], values[1], dt[0], dt[0]);
	for (size_t i = 1; i < deltas.size() - 1; i++)
		deltas[i] = compute_inner_control_point_delta(values[i - 1], values[i], values[i + 1], dt[i - 1], dt[i]);
	deltas.back() = compute_inner_control_point_delta(values[deltas.size() - 2],
	                                                  values[deltas.size() - 1],
	                                                  values[deltas.size() - 1],
	                                                  dt[deltas.size() - 2], dt[deltas.size() - 1]);

	std::vector<double> spline(3 * timestamps.size());
	for (size_t i = 0; i < values.size(); i++)
	{
		if (i > 0)
		{
			// Adjust the inner control points such that velocities remain continuous,
			// even with non-uniform spacing of timestamps.
			// Adjust the incoming inner control point based on the outgoing control point.
			double outgoing = deltas[i];

			double dt0 = dt[i - 1];
			double dt1 = dt[i];
			double t_ratio = dt0 / dt1;

			double q0 = values[i - 1];
			double q1 = values[i];
			double q2 = i + 1 < values.size() ? values[i + 1] : q1;

			double delta_q12 = q2 - q1;
			double delta_q10 = q0 - q1;

			double incoming = 0.5 * (t_ratio * delta_q12 + delta_q10) - t_ratio * outgoing;

			spline[3 * i + 0] = compute_inner_control_point(q1, incoming);
			spline[3 * i + 1] = q1;
			spline[3 * i + 2] = compute_inner_control_point(q1, outgoing);
		}
		else
		{
			double completed = compute_inner_control_point(values[i], deltas[i]);
			spline[3 * i + 0] = completed;
			spline[3 * i + 1] = values[i];
			spline[3 * i + 2] = completed;
		}
	}

	const auto log_results = [&](double T) {
		const double t_offset = 0.001;
		double p0 = eval_squad(timestamps, spline, T - 3.0 * t_offset);
		double p1 = eval_squad(timestamps, spline, T - 2.0 * t_offset);
		double p2 = eval_squad(timestamps, spline, T - 1.0 * t_offset);
		double p3 = eval_squad(timestamps, spline, T);
		double p4 = eval_squad(timestamps, spline, T + 1.0 * t_offset);
		double p5 = eval_squad(timestamps, spline, T + 2.0 * t_offset);
		double p6 = eval_squad(timestamps, spline, T + 3.0 * t_offset);
		double v0 = (p1 - p0) / t_offset;
		double v1 = (p2 - p1) / t_offset;
		double v2 = (p3 - p2) / t_offset;
		double v3 = (p4 - p3) / t_offset;
		double v4 = (p5 - p4) / t_offset;
		double v5 = (p6 - p5) / t_offset;
		double a0 = (v1 - v0) / t_offset;
		double a1 = (v2 - v1) / t_offset;
		double a2 = (v3 - v2) / t_offset;
		double a3 = (v4 - v3) / t_offset;
		double a4 = (v5 - v4) / t_offset;
		double j0 = (a1 - a0) / t_offset;
		double j1 = (a2 - a1) / t_offset;
		double j2 = (a3 - a2) / t_offset;
		double j3 = (a4 - a3) / t_offset;
		LOGI("T = %f\n", T);
		LOGI("p = %f, reference = %f\n", p3, reference_value(T));
		LOGI("\tv0 = %f\n", v0);
		LOGI("\tv1 = %f\n", v1);
		LOGI("\tv2 = %f\n", v2);
		LOGI("\tv3 = %f\n", v3);
		LOGI("\tv4 = %f\n", v4);
		LOGI("\tv5 = %f\n", v5);
		LOGI("\ta0 = %f\n", a0);
		LOGI("\ta1 = %f\n", a1);
		LOGI("\ta2 = %f\n", a2);
		LOGI("\ta3 = %f\n", a3);
		LOGI("\ta4 = %f\n", a4);
		LOGI("\tj0 = %f\n", j0);
		LOGI("\tj1 = %f\n", j1);
		LOGI("\tj2 = %f\n", j2);
		LOGI("\tj3 = %f\n", j3);
	};

	for (auto T : timestamps)
		log_results(T);

	int iterations = int(timestamps.back() * 100);
	double error = 0.0;
	double w = 0.0;
	for (int i = 100; i < iterations - 100; i++)
	{
		double t = double(i) / 100.0;
		double p = eval_squad(timestamps, spline, t);
		double ref_p = reference_value(t);
		//LOGI("t = %f, P = %f, Ref = %f\n", t, p, ref_p);
		error += abs(ref_p - p);
		w += 1.0;
	}

	LOGI("Error = %f\n", error / w);
}

int main()
{
	test_rotation();
	test_translation();
	test_odd_timestamp_slerp();
	test_squad_spline();
}