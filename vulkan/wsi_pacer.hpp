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

#pragma once

#include "small_vector.hpp"
#include <stdint.h>

// Designed for lowest possible latency on fixed rate displays.
// The GPU workload is expected to be very light and stable (e.g. video decoding).
class FixedRefreshRatePacer
{
public:
	FixedRefreshRatePacer();
	~FixedRefreshRatePacer();

	void set_frame_time_ns(uint64_t frame_time);
	void update_feedback(uint64_t present_id, uint64_t queue_done_ns, uint64_t complete_ns);

	// Sleeps as needed. Should be called before sampling inputs, etc.
	void begin_frame_submission(uint64_t current_present_id);

	// This frame will not be valid for purposes of statistics.
	void discard_pacing_statistics(uint64_t present_id);

	// Can avoid some weird interactions with async compute and such.
	void override_gpu_done_time(uint64_t present_id, uint64_t queue_done_ns);

	void reset();

	struct HistogramStats
	{
		double confidence = 0.0;
		uint64_t last_failure_present_id = 0;
		uint64_t num_relevant_present_failures = 0;
	};

	// Debugging.
	HistogramStats get_current_histogram_stats() const;
	HistogramStats get_candidate_histogram_stats() const;
	HistogramStats get_overall_histogram_stats() const;
	uint64_t get_estimated_present_gap_ns() const;
	uint64_t get_estimated_cpu_gpu_idle_latency_ns() const;
	double get_minimum_confidence_for_promotion() const;

private:
	uint64_t frame_time_ns = 0;
	uint64_t estimated_present_gap_ns = UINT64_MAX;
	uint64_t estimated_frame_latency_ns = UINT64_MAX;
	double minimum_confidence_for_promotion = 0.0;

	struct Feedback
	{
		uint64_t present_id;
		uint64_t frame_submission_ns;
		uint64_t estimated_complete_ns;
		uint64_t queue_done_ns;
		uint64_t complete_ns;
	};

	Util::SmallVector<Feedback> feedbacks;
	Feedback last_feedback = {};
	Feedback find_and_remove_feedback(uint64_t present_id);

	// Don't make the quant interval too narrow, or we won't be able capture statistics well.
	enum { GapQuantumNS = 250 * 1000, NumHistogramEntries = 64 };

	HistogramStats histogram[NumHistogramEntries];
	HistogramStats overall;

	void register_present_gap(uint64_t present_id, uint64_t gap_ns, bool success);
	bool safe_to_lower_gap_to(uint64_t present_id, uint64_t observed_gap_ns, uint64_t to_ns) const;
};
