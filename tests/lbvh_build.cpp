#include "muglm/muglm_impl.hpp"
#include "bitops.hpp"
#include "aabb.hpp"
#include <algorithm>
#include <assert.h>
#include <vector>
#include <atomic>
#include <memory>
#include <random>
#include "logging.hpp"
#include "frustum.hpp"
#include "camera.hpp"
#include "render_context.hpp"
#include "simd.hpp"
#include "timer.hpp"
#include "thread_group.hpp"
#include "task_composer.hpp"
#include "radix_sorter.hpp"

using namespace Granite;

struct BVHNode
{
	uint32_t split_index : 30;
	uint32_t left_leaf : 1;
	uint32_t right_leaf : 1;
	uint32_t parent;
	uint32_t range_lo;
	uint32_t range_hi;
};

constexpr int NumPartitions = 64;

static_assert(sizeof(BVHNode) == 16, "Unexpected BVHNode size.");

#if 0
static inline int compute_prefix(uint32_t morton_i, uint32_t morton_j)
{
	return leading_zeroes(morton_i ^ morton_j);
}

static inline int compute_prefix(uint32_t morton_i, uint32_t morton_j, int i, int j)
{
	if (morton_i == morton_j)
		return 32 + compute_prefix(i, j);
	else
		return compute_prefix(morton_i, morton_j);
}
#endif

static inline uint32_t compute_morton_fast(uvec3 icoord)
{
	assert(all(lessThan(icoord, uvec3(1024))));
	icoord = (icoord | (icoord << 16u)) & 0x30000ffu;
	icoord = (icoord | (icoord <<  8u)) & 0x300f00fu;
	icoord = (icoord | (icoord <<  4u)) & 0x30c30c3u;
	icoord = (icoord | (icoord <<  2u)) & 0x9249249u;
	return icoord.x | (icoord.y << 1u) | (icoord.z << 2u);
}

static inline uint32_t compute_morton_slow(uvec3 icoord)
{
	uint32_t code = 0;
	for (unsigned bit = 0; bit < 10; bit++)
	{
		code |= ((icoord.x >> bit) & 1u) << (3 * bit + 0);
		code |= ((icoord.y >> bit) & 1u) << (3 * bit + 1);
		code |= ((icoord.z >> bit) & 1u) << (3 * bit + 2);
	}
	return code;
}

static uint32_t compute_morton(const AABB &aabb, const vec3 &center, float inv_scale)
{
	vec3 dist = inv_scale * (aabb.get_center() - center);
	vec3 divider = abs(dist) + 1.0f;
	vec3 squashed = (1023.0f / 2.0f) * (dist / divider) + (1023.0f / 2.0f);
	uvec3 icoord = uvec3(round(squashed));
	uint32_t result = compute_morton_fast(icoord);
	assert(result == compute_morton_slow(icoord));
	return result;
}

class ConcurrentLBVHBuilder
{
public:
	// Adaptor to pull out pointers to AABB from array of opaque renderables, etc.
	template <typename Op>
	inline void set_aabbs(const Op &op, size_t count)
	{
		aabbs.resize(count);
		for (size_t i = 0; i < count; i++)
			aabbs[i] = &op(i);

		resize_buffers();
	}

	void compute_morton_buffer(TaskComposer &composer, const vec3 &center, float half_point_scale = 0.01f);
	void process(TaskComposer &composer);

	template <typename IntersectOp, typename AcceptOp>
	void intersect(const IntersectOp &intersect,
	               const AcceptOp &on_accept);

	template <typename IntersectOp, typename AcceptOp>
	void intersect_subset(unsigned index, unsigned count,
	                      const IntersectOp &intersect,
	                      const AcceptOp &on_accept);

	const uint32_t *get_code_data() const
	{
		return radix_sorter.code_data();
	}

private:
	template <typename IntersectOp, typename AcceptOp>
	void intersect_from_node(uint32_t base_node_index, const IntersectOp &intersect,
	                         const AcceptOp &on_accept);

	std::vector<const AABB *> aabbs;
	Util::RadixSorter<uint32_t, 8, 8, 8, 6> radix_sorter;
	Util::DynamicArray<uint32_t> leaf_parents;
	Util::DynamicArray<AABB> node_aabbs;
	Util::DynamicArray<BVHNode> nodes;
	std::vector<uint32_t> node_process_list;
	Util::DynamicArray<std::atomic_uint32_t> counters;

	void sort_morton_codes(TaskComposer &composer);
#if 0
	int compute_delta(int i, int j) const;
	int compute_direction(int i) const;
#endif

	void compute_nodes(TaskComposer &composer);
	void compute_nodes_hierarchical_prepass(int index, int lo, int hi, int limit);
	void compute_nodes_hierarchical(int index, int lo, int hi);

#if 0
	void compute_node_concurrent(int i);
	void build_aabbs_bottom_up_concurrent();
	void build_aabbs_bottom_up(size_t i);
#endif
	void complete_aabb_bottom_up(size_t node_index);

	const AABB &get_left_aabb(const BVHNode &node) const;
	const AABB &get_right_aabb(const BVHNode &node) const;

	void resize_buffers();
};

void ConcurrentLBVHBuilder::sort_morton_codes(TaskComposer &composer)
{
	composer.begin_pipeline_stage().enqueue_task([this]() {
		radix_sorter.sort();
	});
}

#if 0
int ConcurrentLBVHBuilder::compute_delta(int i, int j) const
{
	if (j < 0 || j >= int(aabbs.size()))
		return -1;
	auto *codes = radix_sorter.code_data();
	return compute_prefix(codes[i], codes[j], i, j);
}

int ConcurrentLBVHBuilder::compute_direction(int i) const
{
	int left_delta = compute_delta(i, i - 1);
	int right_delta = compute_delta(i, i + 1);
	int s = sign(right_delta - left_delta);
	assert(s != 0);
	return s;
}
#endif

static int find_split_point(const uint32_t *codes, int lo, int hi)
{
	// If lo == hi, we would have emitted leaf nodes already.
	assert(lo != hi);

	uint32_t code_left = codes[lo];
	uint32_t code_right = codes[hi];
	int split;

	if (code_left == code_right)
	{
		// Even split.
		split = lo + ((hi - lo) >> 1);
	}
	else
	{
		uint32_t common_prefix = leading_zeroes(code_left ^ code_right);
		uint32_t bound = code_right & ~(0x7fffffffu >> common_prefix);
		auto itr = std::lower_bound(codes + lo, codes + hi + 1, bound);
		assert(itr >= codes + lo);
		assert(itr <= codes + hi);
		split = int(itr - codes) - 1;
	}

	return split;
}

void ConcurrentLBVHBuilder::compute_nodes_hierarchical(int index, int lo, int hi)
{
	auto *codes = radix_sorter.code_data();
	int split = find_split_point(codes, lo, hi);
	bool left_leaf = lo == split;
	bool right_leaf = hi == split + 1;

	auto &n = nodes[index];
	n.left_leaf = left_leaf;
	n.right_leaf = right_leaf;
	n.split_index = split;
	n.range_lo = lo;
	n.range_hi = hi;

	if (left_leaf)
	{
		leaf_parents[split] = index;
	}
	else
	{
		nodes[split].parent = index;
		compute_nodes_hierarchical(split, lo, split);
	}

	split++;

	if (right_leaf)
	{
		leaf_parents[split] = index;
	}
	else
	{
		nodes[split].parent = index;
		// We could kick off concurrent work here when size of range is a suitable size.
		compute_nodes_hierarchical(split, split, hi);
	}

	auto &left_aabb = get_left_aabb(n);
	auto &right_aabb = get_right_aabb(n);
	node_aabbs[index] = { min(left_aabb.get_minimum(), right_aabb.get_minimum()),
		                  max(left_aabb.get_maximum(), right_aabb.get_maximum()) };
}

void ConcurrentLBVHBuilder::compute_nodes_hierarchical_prepass(int index, int lo, int hi, int limit)
{
	// Defer if we can.
	auto &n = nodes[index];
	n.range_lo = lo;
	n.range_hi = hi;

	if (hi - lo <= limit)
	{
		node_process_list.push_back(index);
		return;
	}

	auto *codes = radix_sorter.code_data();
	int split = find_split_point(codes, lo, hi);
	bool left_leaf = lo == split;
	bool right_leaf = hi == split + 1;

	n.left_leaf = left_leaf;
	n.right_leaf = right_leaf;
	n.split_index = split;

	if (left_leaf)
	{
		leaf_parents[split] = index;
	}
	else
	{
		nodes[split].parent = index;
		compute_nodes_hierarchical_prepass(split, lo, split, limit);
	}

	split++;

	if (right_leaf)
	{
		leaf_parents[split] = index;
	}
	else
	{
		nodes[split].parent = index;
		// We could kick off concurrent work here when size of range is a suitable size.
		compute_nodes_hierarchical_prepass(split, split, hi, limit);
	}
}

#if 0
// Only fast if done massively parallel
void ConcurrentLBVHBuilder::compute_node_concurrent(int i)
{
	int d = compute_direction(i);
	int d_min = compute_delta(i, i - d);

	// Binary search to find the end (O(log n)).
	int l_max = 16;
	while (compute_delta(i, i + l_max * d) > d_min)
		l_max *= 2;

	int l = 0;
	for (int t = l_max >> 1; t >= 1; t >>= 1)
		if (compute_delta(i, i + (l + t) * d) > d_min)
			l += t;

	// Binary search to find the split point.
	int j = i + l * d;
	int d_node = compute_delta(i, j);
	int s = 0;
	int t_shift = 1;
	int t = (l + ((1 << t_shift) - 1)) >> t_shift;
	for (; t >= 1; t_shift++, t = (l + ((1 << t_shift) - 1)) >> t_shift)
		if (compute_delta(i, i + (s + t) * d) > d_node)
			s += t;

	int left_index = min(i, j);
	int right_index = max(i, j);

	int lambda = i + s * d + min(d, 0);
	bool left_leaf = left_index == lambda;
	bool right_leaf = right_index == lambda + 1;
	nodes[i].left_leaf = left_leaf;
	nodes[i].right_leaf = right_leaf;
	nodes[i].split_index = lambda;
	nodes[i].range_lo = left_index;
	nodes[i].range_hi = right_index;

	if (left_leaf)
		leaf_parents[lambda] = i;
	else
		nodes[lambda].parent = i;

	if (right_leaf)
		leaf_parents[lambda + 1] = i;
	else
		nodes[lambda + 1].parent = i;

	if (i == 0)
	{
		// Ensure we terminate traversal later when doing bottom up AABB build.
		nodes[0].parent = 0;
	}
}
#endif

void ConcurrentLBVHBuilder::compute_nodes(TaskComposer &composer)
{
	auto &group = composer.begin_pipeline_stage();
	auto defer = composer.get_deferred_enqueue_handle();
	group.enqueue_task([this, defer]() mutable {
		// Hybrid style. Single thread traversal, then we can go wide.
		int n = int(aabbs.size());
		node_process_list.clear();
		nodes[0].parent = 0;
		compute_nodes_hierarchical_prepass(0, 0, n - 1, (n + NumPartitions - 1) / NumPartitions);
		for (uint32_t work_item : node_process_list)
		{
			defer->enqueue_task([this, work_item]() {
				compute_nodes_hierarchical(int(work_item), int(nodes[work_item].range_lo), int(nodes[work_item].range_hi));
				complete_aabb_bottom_up(work_item);
			});
		}
	});

	// Figure 4 straight implementation.
	// Parallel-for
	//for (int i = 0; i < n - 1; i++)
	//	compute_node_concurrent(i);
}

const AABB &ConcurrentLBVHBuilder::get_left_aabb(const BVHNode &node) const
{
	uint32_t left_index = node.split_index;
	const AABB &left = node.left_leaf ? *aabbs[radix_sorter.indices_data()[left_index]] : node_aabbs[left_index];
	return left;
}

const AABB &ConcurrentLBVHBuilder::get_right_aabb(const BVHNode &node) const
{
	uint32_t right_index = node.split_index + 1;
	const AABB &right = node.right_leaf ? *aabbs[radix_sorter.indices_data()[right_index]] : node_aabbs[right_index];
	return right;
}

void ConcurrentLBVHBuilder::complete_aabb_bottom_up(size_t node_index)
{
	uint32_t parent = nodes[node_index].parent;

	// Atomic increment.
	uint32_t count = counters[parent].fetch_add(1, std::memory_order_acq_rel);

	while (count == 1)
	{
		// Process a parent node.
		auto &node = nodes[parent];
		auto &left = get_left_aabb(node);
		auto &right = get_right_aabb(node);
		node_aabbs[parent] = {
			min(left.get_minimum(), right.get_minimum()),
			max(left.get_maximum(), right.get_maximum()),
		};

		parent = node.parent;
		// Atomic increment.
		count = counters[parent].fetch_add(1, std::memory_order_acq_rel);
	}
}

#if 0
void ConcurrentLBVHBuilder::build_aabbs_bottom_up(size_t i)
{
	uint32_t parent = leaf_parents[i];

	// Atomic increment.
	uint32_t count = counters[parent].fetch_add(1, std::memory_order_acq_rel);

	while (count == 1)
	{
		// Process a parent node.
		auto &node = nodes[parent];
		auto &left = get_left_aabb(node);
		auto &right = get_right_aabb(node);
		node_aabbs[parent] = {
			min(left.get_minimum(), right.get_minimum()),
			max(left.get_maximum(), right.get_maximum()),
		};

		parent = node.parent;
		// Atomic increment.
		count = counters[parent].fetch_add(1, std::memory_order_acq_rel);
	}
}

void ConcurrentLBVHBuilder::build_aabbs_bottom_up_concurrent()
{
	size_t n = aabbs.size();

	// Parallel for.
	for (size_t i = 0; i < n; i++)
		build_aabbs_bottom_up(i);
}
#endif

void ConcurrentLBVHBuilder::resize_buffers()
{
	size_t aabb_count = aabbs.size();
	radix_sorter.resize(aabb_count);
	leaf_parents.reserve(aabb_count);
	if (aabb_count)
	{
		nodes.reserve(aabb_count - 1);
		node_aabbs.reserve(aabb_count - 1);
	}

	counters.reserve(aabb_count);
}

struct DividedRange
{
	size_t start;
	size_t end;
};

static DividedRange divide_range(size_t N, unsigned thread_index, unsigned num_threads)
{
	DividedRange range = {};
	range.start = (N * thread_index) / num_threads;
	range.end = (N * (thread_index + 1)) / num_threads;
	return range;
}

void ConcurrentLBVHBuilder::compute_morton_buffer(TaskComposer &composer, const vec3 &center, float half_point_scale)
{
	assert(radix_sorter.size() == aabbs.size());

	auto &group = composer.begin_pipeline_stage();
	for (unsigned thr = 0; thr < NumPartitions; thr++)
	{
		group.enqueue_task([this, thr, center, half_point_scale]() {
			size_t aabb_count = aabbs.size();
			auto *code = radix_sorter.code_data();
			auto range = divide_range(aabb_count, thr, NumPartitions);
			for (size_t i = range.start; i < range.end; i++)
			{
				code[i] = compute_morton(*aabbs[i], center, half_point_scale);
				counters[i].store(0, std::memory_order_relaxed);
			}
		});
	}
}

void ConcurrentLBVHBuilder::process(TaskComposer &composer)
{
	sort_morton_codes(composer);
	compute_nodes(composer);
}

template <typename IntersectOp, typename AcceptOp>
void ConcurrentLBVHBuilder::intersect_from_node(
	uint32_t base_node_index, const IntersectOp &intersect, const AcceptOp &on_accept)
{
	auto &base_node_aabb = node_aabbs[base_node_index];
	const auto *indices = radix_sorter.indices_data();
	auto &base_node = nodes[base_node_index];
	unsigned stack_index = 0;
	uint32_t node_stack[32];

	SIMD::FrustumCullDualResult result = intersect(base_node_aabb);
	if (result == SIMD::FrustumCullDualResult::Full)
	{
		for (uint32_t lo = base_node.range_lo, hi = base_node.range_hi; lo <= hi; lo++)
		{
			uint32_t remapped_index = indices[lo];
			on_accept(remapped_index);
		}
	}
	else if (result == SIMD::FrustumCullDualResult::Partial)
		node_stack[stack_index++] = base_node_index;

	while (stack_index)
	{
		unsigned index = node_stack[--stack_index];
		auto &n = nodes[index];

		uint32_t left_index = n.split_index;
		uint32_t right_index = n.split_index + 1;

		// Visit left
		if (n.left_leaf)
		{
			uint32_t remapped_index = indices[left_index];
			if (intersect(*aabbs[remapped_index]) != SIMD::FrustumCullDualResult::None)
				on_accept(remapped_index);
		}
		else if ((result = intersect(node_aabbs[left_index])) != SIMD::FrustumCullDualResult::None)
		{
			if (result == SIMD::FrustumCullDualResult::Full)
			{
				auto &l = nodes[left_index];
				for (uint32_t lo = l.range_lo, hi = l.range_hi; lo <= hi; lo++)
					on_accept(indices[lo]);
			}
			else
				node_stack[stack_index++] = left_index;
		}

		// Visit right
		if (n.right_leaf)
		{
			uint32_t remapped_index = indices[right_index];
			if (intersect(*aabbs[remapped_index]) != SIMD::FrustumCullDualResult::None)
				on_accept(remapped_index);
		}
		else if ((result = intersect(node_aabbs[right_index])) != SIMD::FrustumCullDualResult::None)
		{
			if (result == SIMD::FrustumCullDualResult::Full)
			{
				auto &r = nodes[right_index];
				for (uint32_t lo = r.range_lo, hi = r.range_hi; lo <= hi; lo++)
					on_accept(indices[lo]);
			}
			else
				node_stack[stack_index++] = right_index;
		}

		assert(stack_index <= 32);
	}
}

template <typename IntersectOp, typename AcceptOp>
void ConcurrentLBVHBuilder::intersect(const IntersectOp &intersect,
                                      const AcceptOp &on_accept)
{
	if (aabbs.empty())
	{
		return;
	}
	else if (aabbs.size() == 1)
	{
		if (intersect(*aabbs.front()) != SIMD::FrustumCullDualResult::None)
			on_accept(0);
		return;
	}

	intersect_from_node(0, intersect, on_accept);
}

template <typename IntersectOp, typename AcceptOp>
void ConcurrentLBVHBuilder::intersect_subset(unsigned index, unsigned count,
                                             const IntersectOp &intersect,
                                             const AcceptOp &on_accept)
{
	if (aabbs.size() < 2)
	{
		if (index == 0 && aabbs.size() == 1)
		{
			if (intersect(*aabbs.front()) != SIMD::FrustumCullDualResult::None)
				on_accept(0);
		}
		return;
	}

	auto r = divide_range(node_process_list.size(), index, count);
	for (size_t i = r.start; i < r.end; i++)
		intersect_from_node(node_process_list[i], intersect, on_accept);
}

int main()
{
	ThreadGroup group;
	group.start(8, {});

	TaskComposer composer(group);

	std::vector<AABB> aabbs;
	std::default_random_engine rnd(42);
	std::uniform_real_distribution<float> dist_center(-3.0f, 3.0f);
	std::uniform_real_distribution<float> dist_range(0.1f, 0.3f);
	constexpr size_t N = 19670;
	aabbs.reserve(N);
	for (size_t i = 0; i < N; i++)
	{
		vec3 center;
		vec3 range;

		center.x = dist_center(rnd);
		center.y = dist_center(rnd);
		center.z = dist_center(rnd);
		range.x = dist_range(rnd);
		range.y = dist_range(rnd);
		range.z = dist_range(rnd);
		aabbs.emplace_back(center - range, center + range);
	}

	Util::Timer timer;
	ConcurrentLBVHBuilder builder;
	builder.set_aabbs([&](size_t i) -> const AABB & { return aabbs[i]; }, aabbs.size());

	struct M { uint32_t code; uint32_t index; };
	std::vector<M> ms;
	ms.reserve(aabbs.size());

	timer.start();
	builder.compute_morton_buffer(composer, vec3(0.0f), 1.0f);
	builder.process(composer);
	composer.get_outgoing_task()->wait();
	LOGI("Process time: %.3f ms.\n", 1e3 * timer.end());

	RenderContext ctx;
	Frustum f;
	Camera c;

	c.set_aspect(1.0f);
	c.set_fovy(1.0f);
	c.set_depth_range(0.1f, 10.0f);
	c.look_at(vec3(0.0f), vec3(0.3f, 0.2f, 0.5f));
	ctx.set_camera(c);
	f.build_planes(ctx.get_render_parameters().inv_view_projection);

	constexpr int NumTraceTasks = 8;
	std::vector<uint32_t> visible_indices[NumTraceTasks];
	std::vector<uint32_t> brute_force_indices[NumTraceTasks];

	for (auto &indices : visible_indices)
		indices.clear();
	timer.start();
	for (int i = 0; i < 100; i++)
	{
		auto &g = composer.begin_pipeline_stage();
		for (int j = 0; j < NumTraceTasks; j++)
		{
			g.enqueue_task([&, j]() {
				builder.intersect_subset(j, NumTraceTasks,
						[&](const AABB &aabb) { return SIMD::frustum_cull_dual(aabb, f.get_planes()); },
						[&](uint32_t index) { visible_indices[j].push_back(index); });
			});
		}
	}
	composer.get_outgoing_task()->wait();
	LOGI("Intersect time: %.3f ms\n", 1e3 * timer.end());

	for (auto &indices : brute_force_indices)
		indices.clear();
	timer.start();
	for (int i = 0; i < 100; i++)
	{
		auto &g = composer.begin_pipeline_stage();
		for (int j = 0; j < NumTraceTasks; j++)
		{
			g.enqueue_task([&, j]() {
			  auto r = divide_range(aabbs.size(), j, NumTraceTasks);
			  for (size_t index = r.start; index < r.end; index++)
			  {
				  if (SIMD::frustum_cull(aabbs[index], f.get_planes()))
					  brute_force_indices[j].push_back(index);
			  }
			});
		}
	}
	composer.get_outgoing_task()->wait();
	LOGI("Naive intersect time: %.3f ms\n", 1e3 * timer.end());

	for (int j = 1; j < NumTraceTasks; j++)
	{
		visible_indices[0].insert(visible_indices[0].end(),
		                          visible_indices[j].begin(),
		                          visible_indices[j].end());

		brute_force_indices[0].insert(brute_force_indices[0].end(),
		                              brute_force_indices[j].begin(),
		                              brute_force_indices[j].end());
	}

	std::sort(visible_indices[0].begin(), visible_indices[0].end());
	std::sort(brute_force_indices[0].begin(), brute_force_indices[0].end());

	LOGI("Visible count: %zu\n", visible_indices[0].size());
	LOGI("Brute force count: %zu\n", brute_force_indices[0].size());

	assert(brute_force_indices[0].size() == visible_indices[0].size());
	assert(memcmp(brute_force_indices[0].data(), visible_indices[0].data(),
	              visible_indices[0].size() * sizeof(uint32_t)) == 0);

	return 0;
}