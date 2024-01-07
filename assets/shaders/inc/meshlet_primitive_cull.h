#ifndef MESHLET_PRIMITIVE_CULL_H_
#define MESHLET_PRIMITIVE_CULL_H_

#if defined(MESHLET_PRIMITIVE_CULL_WAVE32_DUAL) && MESHLET_PRIMITIVE_CULL_WAVE32_DUAL
#define MESHLET_PRIMITIVE_CULL_DUAL
#endif

#ifdef MESHLET_PRIMITIVE_CULL_DUAL
uint shared_active_vert_count_total;
uint shared_active_prim_count_total;
uint shared_active_vert_mask;
#define meshlet_setup_local_invocation(id)
#else
shared uint shared_active_vert_count[gl_WorkGroupSize.x / 64u];
shared uint shared_active_prim_count[gl_WorkGroupSize.x / 32u];
shared uint shared_active_prim_mask[gl_WorkGroupSize.x / 32u];
shared uint shared_active_vert_mask[gl_WorkGroupSize.x / 64u];
shared uint shared_active_vert_count_total;
shared uint shared_active_prim_count_total;
shared vec2 shared_window_positions[gl_WorkGroupSize.x / 64u][32];
shared uint8_t shared_clip_code[gl_WorkGroupSize.x / 64u][32];

uint LocalInvocationIndex;
uvec2 LocalInvocationID;

void meshlet_setup_local_invocation(uint local_id, uint lane_index, uint chunk_index)
{
    LocalInvocationIndex = local_id;
	LocalInvocationID = uvec2(lane_index, chunk_index);
}
#endif

const uint CLIP_CODE_INACCURATE = 1 << 0;
const uint CLIP_CODE_NEGATIVE_W = 1 << 1;
const uint CLIP_CODE_NEGATIVE_X = 1 << 2;
const uint CLIP_CODE_NEGATIVE_Y = 1 << 3;
const uint CLIP_CODE_POSITIVE_X = 1 << 4;
const uint CLIP_CODE_POSITIVE_Y = 1 << 5;
const uint CLIP_CODE_PLANES = uint(-1) & ~CLIP_CODE_INACCURATE;

#ifdef MESHLET_PRIMITIVE_CULL_DUAL
uint compacted_vertex_output(uint index)
{
    return bitCount(bitfieldExtract(shared_active_vert_mask, 0, int(index)));
}

uint meshlet_compacted_vertex_output()
{
    return compacted_vertex_output(gl_SubgroupInvocationID);
}

bool meshlet_lane_has_active_vert()
{
    return (shared_active_vert_mask & (1u << gl_SubgroupInvocationID)) != 0u;
}
#else
uint compacted_vertex_output(uint index)
{
	uint base = shared_active_vert_count[LocalInvocationID.y];
    return base + bitCount(bitfieldExtract(shared_active_vert_mask[LocalInvocationID.y], 0, int(index)));
}

uint meshlet_compacted_vertex_output()
{
    return compacted_vertex_output(LocalInvocationID.x);
}

uint compacted_index_output()
{
	uint base = shared_active_prim_count[LocalInvocationIndex / 32];
	return base + bitCount(bitfieldExtract(
			shared_active_prim_mask[LocalInvocationIndex / 32], 0, int(LocalInvocationIndex & 31)));
}

bool meshlet_lane_has_active_vert()
{
    return LocalInvocationID.x < 32 && (shared_active_vert_mask[LocalInvocationID.y] & (1u << LocalInvocationID.x)) != 0u;
}

void meshlet_init_shared()
{
	uint num_chunks = gl_WorkGroupSize.x / 64u;
	if (gl_LocalInvocationIndex < num_chunks)
		shared_active_vert_mask[gl_LocalInvocationIndex] = 0;
	if (gl_LocalInvocationIndex < 2 * num_chunks)
		shared_active_prim_mask[gl_LocalInvocationIndex] = 0;
}
#endif

uvec3 remap_index_buffer(uvec3 prim)
{
    return uvec3(compacted_vertex_output(prim.x), compacted_vertex_output(prim.y), compacted_vertex_output(prim.z));
}

bool cull_triangle(vec2 a, vec2 b, vec2 c)
{
    // To be completely accurate, this should be done in fixed point,
    // but we can YOLO a bit since glitches in extreme edge cases are considered okay.
    precise vec2 ab = b - a;
    precise vec2 ac = c - a;

    // This is 100% accurate as long as the primitive is no larger than ~4k subpixels, i.e. 16x16 pixels.
    // Normally, we'd be able to do GEQ test, but GE test is conservative, even with FP error in play.
    precise float pos_area = ab.y * ac.x;
    precise float neg_area = ab.x * ac.y;

    // If the pos value is (-2^24, +2^24), the FP math is exact, if not, we have to be conservative.
    // Less-than check is there to ensure that 1.0 delta in neg_area *will* resolve to a different value.
    bool active_primitive;
    if (abs(pos_area) < 16777216.0)
        active_primitive = pos_area > neg_area;
    else
        active_primitive = pos_area >= neg_area;

    if (active_primitive)
    {
        // Micropoly test.
        const int SUBPIXEL_BITS = 8;
        vec2 lo = floor(ldexp(min(min(a, b), c), ivec2(-SUBPIXEL_BITS)));
        vec2 hi = floor(ldexp(max(max(a, b), c), ivec2(-SUBPIXEL_BITS)));
        active_primitive = all(notEqual(lo, hi));
    }

    return active_primitive;
}

uint meshlet_get_meshlet_index()
{
    if (gl_WorkGroupSize.x != 256)
        return gl_WorkGroupID.y;
    else
        return gl_WorkGroupID.x;
}

uint meshlet_get_base_chunk_index()
{
#ifdef MESHLET_PRIMITIVE_CULL_DUAL
	return gl_WorkGroupID.x;
#else
    if (gl_WorkGroupSize.x != 256)
        return gl_WorkGroupID.x * (gl_WorkGroupSize.x / 64u);
    else
        return 0;
#endif
}

#ifdef MESHLET_PRIMITIVE_CULL_DUAL
bool meshlet_is_active_primitive(uvec3 prim, vec2 window, uint clip_code)
{
	vec2 window_a = subgroupShuffle(window, prim.x);
	vec2 window_b = subgroupShuffle(window, prim.y);
	vec2 window_c = subgroupShuffle(window, prim.z);
    uint code_a = subgroupShuffle(clip_code, prim.x);
    uint code_b = subgroupShuffle(clip_code, prim.y);
    uint code_c = subgroupShuffle(clip_code, prim.z);

    uint or_code = code_a | code_b | code_c;
    uint and_code = code_a & code_b & code_c;

    bool culled_planes = (and_code & CLIP_CODE_PLANES) != 0;
    bool is_active_prim = false;

    if (!culled_planes)
    {
        is_active_prim = (or_code & (CLIP_CODE_INACCURATE | CLIP_CODE_NEGATIVE_W)) != 0;
        if (!is_active_prim)
            is_active_prim = cull_triangle(window_a, window_b, window_c);
    }

	return is_active_prim;
}

void meshlet_emit_primitive_dual(uvec3 prim_lo, uvec3 prim_hi, vec4 clip_pos, vec4 viewport)
{
    vec2 c = clip_pos.xy / clip_pos.w;

    uint clip_code = clip_pos.w <= 0.0 ? CLIP_CODE_NEGATIVE_W : 0;
    if (any(greaterThan(abs(c), vec2(4.0))))
        clip_code |= CLIP_CODE_INACCURATE;
    if (c.x <= -1.0)
        clip_code |= CLIP_CODE_NEGATIVE_X;
    if (c.y <= -1.0)
        clip_code |= CLIP_CODE_NEGATIVE_Y;
    if (c.x >= 1.0)
        clip_code |= CLIP_CODE_POSITIVE_X;
    if (c.y >= 1.0)
        clip_code |= CLIP_CODE_POSITIVE_Y;

    vec2 window = roundEven(c * viewport.zw + viewport.xy);

	bool is_active_prim_lo = meshlet_is_active_primitive(prim_lo, window, clip_code);
	bool is_active_prim_hi = meshlet_is_active_primitive(prim_hi, window, clip_code);

	uint vert_mask = 0u;
	if (is_active_prim_lo)
		vert_mask |= (1u << prim_lo.x) | (1u << prim_lo.y) | (1u << prim_lo.z);
	if (is_active_prim_hi)
		vert_mask |= (1u << prim_hi.x) | (1u << prim_hi.y) | (1u << prim_hi.z);

	shared_active_vert_mask = subgroupOr(vert_mask);
	shared_active_vert_count_total = bitCount(shared_active_vert_mask);

	uvec4 prim_ballot_lo = subgroupBallot(is_active_prim_lo);
	uvec4 prim_ballot_hi = subgroupBallot(is_active_prim_hi);
	uint prim_offset_lo = subgroupBallotExclusiveBitCount(prim_ballot_lo);
	uint prim_offset_hi = subgroupBallotExclusiveBitCount(prim_ballot_hi);
	uint prim_count_lo = subgroupBallotBitCount(prim_ballot_lo);
	uint prim_count_hi = subgroupBallotBitCount(prim_ballot_hi);

	shared_active_prim_count_total = prim_count_lo + prim_count_hi;
    SetMeshOutputsEXT(shared_active_vert_count_total, shared_active_prim_count_total);

    if (is_active_prim_lo)
	    gl_PrimitiveTriangleIndicesEXT[prim_offset_lo] = remap_index_buffer(prim_lo);
    if (is_active_prim_hi)
	    gl_PrimitiveTriangleIndicesEXT[prim_offset_hi + prim_count_lo] = remap_index_buffer(prim_hi);
}
#else
void meshlet_emit_primitive(uvec3 prim, vec4 clip_pos, vec4 viewport)
{
	meshlet_init_shared();

	if (LocalInvocationID.x < 32)
	{
		vec2 c = clip_pos.xy / clip_pos.w;

		uint clip_code = clip_pos.w <= 0.0 ? CLIP_CODE_NEGATIVE_W : 0;
		if (any(greaterThan(abs(c), vec2(4.0))))
			clip_code |= CLIP_CODE_INACCURATE;
		if (c.x <= -1.0)
			clip_code |= CLIP_CODE_NEGATIVE_X;
		if (c.y <= -1.0)
			clip_code |= CLIP_CODE_NEGATIVE_Y;
		if (c.x >= 1.0)
			clip_code |= CLIP_CODE_POSITIVE_X;
		if (c.y >= 1.0)
			clip_code |= CLIP_CODE_POSITIVE_Y;

		vec2 window = roundEven(c * viewport.zw + viewport.xy);
		shared_window_positions[LocalInvocationID.y][LocalInvocationID.x] = window;
		shared_clip_code[LocalInvocationID.y][LocalInvocationID.x] = uint8_t(clip_code);
	}

    barrier();

    uint code_a = shared_clip_code[LocalInvocationID.y][prim.x];
    uint code_b = shared_clip_code[LocalInvocationID.y][prim.y];
    uint code_c = shared_clip_code[LocalInvocationID.y][prim.z];

    uint or_code = code_a | code_b | code_c;
    uint and_code = code_a & code_b & code_c;

    bool culled_planes = (and_code & CLIP_CODE_PLANES) != 0;
    bool is_active_prim = false;

    if (!culled_planes)
    {
        is_active_prim = (or_code & (CLIP_CODE_INACCURATE | CLIP_CODE_NEGATIVE_W)) != 0;

        if (!is_active_prim)
        {
            vec2 window_a = shared_window_positions[LocalInvocationID.y][prim.x];
            vec2 window_b = shared_window_positions[LocalInvocationID.y][prim.y];
            vec2 window_c = shared_window_positions[LocalInvocationID.y][prim.z];
            is_active_prim = cull_triangle(window_a, window_b, window_c);
        }
    }

	if (is_active_prim)
	{
		uint vert_mask = (1u << prim.x) | (1u << prim.y) | (1u << prim.z);
		atomicOr(shared_active_prim_mask[LocalInvocationIndex / 32], 1u << (LocalInvocationIndex & 31));
		atomicOr(shared_active_vert_mask[LocalInvocationID.y], vert_mask);
	}

    barrier();

    if (gl_LocalInvocationIndex == 0)
    {
	    uint active_prim = 0;
	    uint active_vert = 0;

		uint num_chunks = gl_WorkGroupSize.x / 64u;

	    for (uint i = 0; i < num_chunks; i++)
	    {
		    shared_active_prim_count[i] = active_prim;
		    shared_active_vert_count[i] = active_vert;
		    uint prim_count = bitCount(shared_active_prim_mask[i]);
		    uint vert_count = bitCount(shared_active_vert_mask[i]);
		    active_prim += prim_count;
		    active_vert += vert_count;
	    }

	    for (uint i = num_chunks; i < 2 * num_chunks; i++)
	    {
		    shared_active_prim_count[i] = active_prim;
		    uint prim_count = bitCount(shared_active_prim_mask[i]);
		    active_prim += prim_count;
	    }

	    shared_active_prim_count_total = active_prim;
	    shared_active_vert_count_total = active_vert;
    }

    barrier();

    SetMeshOutputsEXT(shared_active_vert_count_total, shared_active_prim_count_total);

    if (is_active_prim)
	    gl_PrimitiveTriangleIndicesEXT[compacted_index_output()] = remap_index_buffer(prim);
}
#endif

#endif
