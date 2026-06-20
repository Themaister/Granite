/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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

#include "wsi_pacer.hpp"
#include "timer.hpp"

FixedRefreshRatePacer::FixedRefreshRatePacer()
{
	reset();
}

FixedRefreshRatePacer::~FixedRefreshRatePacer()
{
#if 0
	for (int i = 0; i < NumHistogramEntries; i++)
	{
		LOGI("Success rate: band [%.3f ms - %.3f ms] -> %.3f %% (last failure %llu) (overall %llu failures)\n", double(i * GapQuantumNS) * 1e-6,
		     double((i + 1) * GapQuantumNS) * 1e-6, histogram[i].confidence * 100.0,
		     static_cast<unsigned long long>(histogram[i].last_failure_present_id),
		     static_cast<unsigned long long>(histogram[i].num_relevant_present_failures));
	}
#endif
}

void FixedRefreshRatePacer::reset()
{
	feedbacks.clear();
	last_feedback = {};
	frame_time_ns = 0;
	estimated_present_gap_ns = UINT64_MAX;
	estimated_frame_latency_ns = UINT64_MAX;
	for (auto &entry : histogram)
		entry = {};
	overall = {};
	minimum_confidence_for_promotion = 0.0;
}

void FixedRefreshRatePacer::discard_pacing_statistics(uint64_t present_id)
{
	find_and_remove_feedback(present_id);
}

FixedRefreshRatePacer::Feedback FixedRefreshRatePacer::find_and_remove_feedback(uint64_t present_id)
{
	auto itr = std::find_if(feedbacks.begin(), feedbacks.end(), [=](const Feedback &f)
	{
		return f.present_id == present_id;
	});

	if (itr == feedbacks.end())
		return {};

	auto ret = *itr;
	feedbacks.erase(itr);
	return ret;
}

void FixedRefreshRatePacer::set_frame_time_ns(uint64_t frame_time)
{
	frame_time_ns = frame_time;

	// Dynamically adjust as we get completed frames coming in.
	estimated_present_gap_ns = std::min<uint64_t>(frame_time_ns / 2, estimated_present_gap_ns);

	// Stay in the center of the histogram range to avoid annoying rounding errors when quantizing results
	// to histogram.
	estimated_present_gap_ns = (estimated_present_gap_ns / GapQuantumNS) * GapQuantumNS + (GapQuantumNS / 2);
}

void FixedRefreshRatePacer::override_gpu_done_time(uint64_t present_id, uint64_t queue_done_ns)
{
	auto itr =
	    std::find_if(feedbacks.begin(), feedbacks.end(), [=](const Feedback &f) { return f.present_id == present_id; });
	if (itr != feedbacks.end())
		itr->queue_done_ns = queue_done_ns;
}

void FixedRefreshRatePacer::update_feedback(uint64_t present_id, uint64_t queue_done_ns, uint64_t complete_ns)
{
	auto feedback = find_and_remove_feedback(present_id);

	if (feedback.queue_done_ns)
		queue_done_ns = feedback.queue_done_ns;

	last_feedback.present_id = present_id;
	last_feedback.queue_done_ns = queue_done_ns;
	last_feedback.complete_ns = complete_ns;

	if (feedback.present_id != present_id)
		return;

	// Safety.
	queue_done_ns = std::min<uint64_t>(queue_done_ns, complete_ns);

	// The present happened when we expected it to. If it falls meaningfully outside of this, we have VRR
	// and a FRR pacer isn't super useful to begin with.
	if (complete_ns <= feedback.estimated_complete_ns + frame_time_ns / 8)
	{
		// With an ideal setup queue_done_ns can be arbitrarily close to complete_ns before we start dropping frames,
		// but that's not realistic.
		uint64_t present_gap_ns = complete_ns - queue_done_ns;

		if (estimated_present_gap_ns == UINT64_MAX)
		{
			// Initialize the estimate.
			estimated_present_gap_ns = frame_time_ns / 2;
		}

		register_present_gap(present_id, present_gap_ns, true);

		if (present_gap_ns <= estimated_present_gap_ns)
		{
			// The compositor was able to deal with current tight timings, update our estimate accordingly (slowly).
			uint64_t candidate_ns = std::max<uint64_t>(estimated_present_gap_ns, GapQuantumNS) - GapQuantumNS;

			// Only lower if history shows this to be a sound decision.
			if (safe_to_lower_gap_to(present_id, present_gap_ns, candidate_ns))
				estimated_present_gap_ns = candidate_ns;
		}
	}
	else if (complete_ns > feedback.estimated_complete_ns + frame_time_ns / 2 &&
	         queue_done_ns < feedback.estimated_complete_ns)
	{
		// The GPU was done in time, but we dropped a frame regardless.
		// Likely the compositor has a queueing delay we need to consider.

		estimated_present_gap_ns += GapQuantumNS;
		uint64_t potential_present_gap_ns = feedback.estimated_complete_ns - queue_done_ns;

		// If the present gap is over half a frame, it's likely the compositor just derping out for no good reason.
		if (potential_present_gap_ns < frame_time_ns / 2)
		{
			// We have a potential present gap that failed to flip properly.
			register_present_gap(present_id, potential_present_gap_ns, false);
		}
	}

	// Otherwise the GPU was simply too slow to render.
	// The frame latency will have to increase as expected.

	uint64_t frame_latency_ns = std::max<uint64_t>(feedback.frame_submission_ns, queue_done_ns) -
	                            feedback.frame_submission_ns;

	// Keep a running estimate of how long time it takes to process CPU -> GPU.
	// This will inform sleep targets.
	if (estimated_frame_latency_ns != UINT64_MAX)
		estimated_frame_latency_ns = (estimated_frame_latency_ns * 15 + frame_latency_ns) / 16;
	else
		estimated_frame_latency_ns = frame_latency_ns;
}

void FixedRefreshRatePacer::register_present_gap(uint64_t present_id, uint64_t gap_ns, bool success)
{
	if (!success)
	{
		overall.confidence *= 0.5;
		overall.last_failure_present_id = present_id;
		overall.num_relevant_present_failures++;
	}
	else
	{
		// This estimate should move very slowly.
		overall.confidence = overall.confidence * 0.99 + 0.01;
		if (overall.confidence > 0.9999999)
		{
			if (overall.num_relevant_present_failures)
			{
				// If we're resetting, put a higher bar of confidence if we want to promote so
				// that we don't instantly start to see failures.
				minimum_confidence_for_promotion = 0.7499 + minimum_confidence_for_promotion * 0.25;
			}

			overall.num_relevant_present_failures = 0;
		}
	}

	int index = int(gap_ns / GapQuantumNS);
	if (index >= NumHistogramEntries)
		return;

	auto &entry = histogram[index];

	if (!success)
	{
		// Avoid denorm hell, but also reset the counter if we cannot get reliable successes multiple times in a row.
		if (entry.confidence < 0.25)
			entry.confidence = 0.0;
		entry.confidence = entry.confidence * 0.5;
		entry.last_failure_present_id = present_id;
		entry.num_relevant_present_failures++;
	}
	else
	{
		// This gap seems super stable now.
		entry.confidence = entry.confidence * 0.95 + 0.05;
		if (entry.confidence > 0.999999)
			entry.num_relevant_present_failures = 0;
	}
}

FixedRefreshRatePacer::HistogramStats FixedRefreshRatePacer::get_current_histogram_stats() const
{
	if (estimated_present_gap_ns == UINT64_MAX)
		return {};

	int index = int(estimated_present_gap_ns / GapQuantumNS);
	if (index >= NumHistogramEntries)
		return {};
	return histogram[index];
}

double FixedRefreshRatePacer::get_minimum_confidence_for_promotion() const
{
	return minimum_confidence_for_promotion;
}

FixedRefreshRatePacer::HistogramStats FixedRefreshRatePacer::get_candidate_histogram_stats() const
{
	if (estimated_present_gap_ns == UINT64_MAX)
		return {};

	int index = int(estimated_present_gap_ns / GapQuantumNS) - 1;
	if (index >= NumHistogramEntries || index < 0)
		return {};
	return histogram[index];
}

FixedRefreshRatePacer::HistogramStats FixedRefreshRatePacer::get_overall_histogram_stats() const
{
	return overall;
}

uint64_t FixedRefreshRatePacer::get_estimated_present_gap_ns() const
{
	return estimated_present_gap_ns;
}

uint64_t FixedRefreshRatePacer::get_estimated_cpu_gpu_idle_latency_ns() const
{
	return estimated_frame_latency_ns;
}

bool FixedRefreshRatePacer::safe_to_lower_gap_to(uint64_t present_id, uint64_t observed_gap_ns, uint64_t to_ns) const
{
	// If we can prove some safety of the target gap, go for it.
	int to_index = int(to_ns / GapQuantumNS);
	int from_index = int(observed_gap_ns / GapQuantumNS);
	if (to_index >= NumHistogramEntries || from_index >= NumHistogramEntries)
		return true;

	// Don't lower until overall success probability is solid.
	uint64_t frames_since_last_observed_failure = present_id - overall.last_failure_present_id;
	auto num_failures = std::min<unsigned>(16, overall.num_relevant_present_failures);
	if (frames_since_last_observed_failure < (1u << num_failures))
		return false;

	// We're confident the update will work well.
	if (histogram[to_index].confidence >= 0.999)
		return true;

	// The bar to clear is higher now. If we never saw any failures, go ahead, we need to learn.
	if (histogram[to_index].confidence < minimum_confidence_for_promotion &&
	    histogram[to_index].num_relevant_present_failures != 0)
		return false;

	// We haven't proved this rate is solid.
	if (histogram[from_index].confidence < 0.999)
		return false;

	// We've never observed a failure at this gap before, should be good to test until we observe failures.
	if (histogram[to_index].last_failure_present_id == 0)
		return true;

	// Check statistics per histogram.
	frames_since_last_observed_failure = present_id - histogram[to_index].last_failure_present_id;

	// Backoff algorithm which scales the number of safety frames based on failures observed overall.
	// Eventually, we have enough failures that we never trust it blindly.
	num_failures = std::min<unsigned>(16, histogram[to_index].num_relevant_present_failures);
	return frames_since_last_observed_failure >= (60u << num_failures);
}

void FixedRefreshRatePacer::begin_frame_submission(uint64_t current_present_id)
{
	if (last_feedback.present_id == 0 || frame_time_ns == 0)
		return;

	uint64_t estimated_complete_ns =
			(current_present_id - last_feedback.present_id) * frame_time_ns +
			last_feedback.complete_ns;

	if (estimated_present_gap_ns != UINT64_MAX)
	{
		uint64_t target_gap_ns = estimated_present_gap_ns;
		target_gap_ns += estimated_frame_latency_ns;
		int64_t sleep_target_ns = int64_t(estimated_complete_ns) - int64_t(target_gap_ns);
		Util::sleep_until_nsecs(sleep_target_ns);
	}

	// Safety clear in case it's never polled.
	if (feedbacks.size() > 16)
		feedbacks.clear();

	feedbacks.push_back({ current_present_id, uint64_t(Util::get_current_time_nsecs()), estimated_complete_ns });
}
