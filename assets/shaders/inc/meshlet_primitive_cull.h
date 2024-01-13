#ifndef MESHLET_PRIMITIVE_CULL_H_
#define MESHLET_PRIMITIVE_CULL_H_

#define CULL_MODE_WG32 0
#define CULL_MODE_WAVE32 1
#define CULL_MODE_GENERIC 2

#if defined(MESHLET_PRIMITIVE_CULL_WG32) && MESHLET_PRIMITIVE_CULL_WG32
#define CULL_MODE CULL_MODE_WG32
#elif defined(MESHLET_PRIMITIVE_CULL_WAVE32) && MESHLET_PRIMITIVE_CULL_WAVE32
#define CULL_MODE CULL_MODE_WAVE32
#else
#define CULL_MODE CULL_MODE_GENERIC
#endif

#if CULL_MODE == CULL_MODE_WG32
uint shared_active_vert_count_total;
uint shared_active_prim_count_total;
#else
shared uint shared_active_vert_count[gl_WorkGroupSize.y];
shared uint shared_active_prim_count[gl_WorkGroupSize.y];
shared uint shared_active_vert_count_total;
shared uint shared_active_prim_count_total;
#endif

#if CULL_MODE != CULL_MODE_GENERIC
uint shared_active_vert_mask;
uint shared_active_prim_offset;
#endif

#if CULL_MODE == CULL_MODE_GENERIC
shared uint shared_active_vert_mask[gl_WorkGroupSize.y];
shared uint shared_active_prim_mask[gl_WorkGroupSize.y];
shared vec2 shared_window_positions[gl_WorkGroupSize.y][gl_WorkGroupSize.x];
shared uint8_t shared_clip_code[gl_WorkGroupSize.y][gl_WorkGroupSize.x];
uvec2 LocalInvocationID;

void meshlet_setup_local_invocation(uvec2 local_id)
{
    LocalInvocationID = local_id;
}
#else
#define meshlet_setup_local_invocation(id)
#endif

const uint CLIP_CODE_INACCURATE = 1 << 0;
const uint CLIP_CODE_NEGATIVE_W = 1 << 1;
const uint CLIP_CODE_NEGATIVE_X = 1 << 2;
const uint CLIP_CODE_NEGATIVE_Y = 1 << 3;
const uint CLIP_CODE_POSITIVE_X = 1 << 4;
const uint CLIP_CODE_POSITIVE_Y = 1 << 5;
const uint CLIP_CODE_PLANES = uint(-1) & ~CLIP_CODE_INACCURATE;

#if CULL_MODE == CULL_MODE_WG32
uint compacted_vertex_output(uint index)
{
    return bitCount(bitfieldExtract(shared_active_vert_mask, 0, int(index)));
}

uint meshlet_compacted_vertex_output()
{
    return compacted_vertex_output(gl_SubgroupInvocationID);
}

uint compacted_index_output()
{
    return shared_active_prim_offset;
}

bool meshlet_lane_has_active_vert()
{
    return (shared_active_vert_mask & (1u << gl_SubgroupInvocationID)) != 0u;
}
#elif CULL_MODE == CULL_MODE_WAVE32
uint compacted_vertex_output(uint index)
{
    return shared_active_vert_count[gl_SubgroupID] + bitCount(bitfieldExtract(shared_active_vert_mask, 0, int(index)));
}

uint meshlet_compacted_vertex_output()
{
    return compacted_vertex_output(gl_SubgroupInvocationID);
}

uint compacted_index_output()
{
    return shared_active_prim_count[gl_SubgroupID] + shared_active_prim_offset;
}

bool meshlet_lane_has_active_vert()
{
    return (shared_active_vert_mask & (1u << gl_SubgroupInvocationID)) != 0u;
}
#else
uint compacted_vertex_output(uint index)
{
    return shared_active_vert_count[LocalInvocationID.y] +
        bitCount(bitfieldExtract(shared_active_vert_mask[LocalInvocationID.y], 0, int(index)));
}

uint meshlet_compacted_vertex_output()
{
    return compacted_vertex_output(LocalInvocationID.x);
}

uint compacted_index_output()
{
    return shared_active_prim_count[LocalInvocationID.y] +
        bitCount(bitfieldExtract(shared_active_prim_mask[LocalInvocationID.y], 0, int(LocalInvocationID.x)));
}

bool meshlet_lane_has_active_vert()
{
    return (shared_active_vert_mask[LocalInvocationID.y] & (1u << LocalInvocationID.x)) != 0u;
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

#if CULL_MODE == CULL_MODE_GENERIC
void meshlet_init_shared()
{
    if (gl_LocalInvocationIndex < gl_WorkGroupSize.y)
    {
        shared_active_vert_mask[gl_LocalInvocationIndex] = 0;
        shared_active_prim_mask[gl_LocalInvocationIndex] = 0;
    }
}
#endif

uint meshlet_get_meshlet_index()
{
	return gl_WorkGroupSize.y == 8 ? gl_WorkGroupID.x : gl_WorkGroupID.y;
}

uint meshlet_get_sublet_index(uint sublet_index)
{
	if (gl_WorkGroupSize.y == 8)
		return sublet_index;
	else
		return gl_WorkGroupSize.y * gl_WorkGroupID.x + sublet_index;
}

void meshlet_emit_primitive(uvec3 prim, vec4 clip_pos, vec4 viewport)
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

#if CULL_MODE != CULL_MODE_GENERIC
	vec2 window_a = subgroupShuffle(window, prim.x);
	vec2 window_b = subgroupShuffle(window, prim.y);
	vec2 window_c = subgroupShuffle(window, prim.z);
    uint code_a = subgroupShuffle(clip_code, prim.x);
    uint code_b = subgroupShuffle(clip_code, prim.y);
    uint code_c = subgroupShuffle(clip_code, prim.z);
#else
    meshlet_init_shared();
    shared_window_positions[LocalInvocationID.y][LocalInvocationID.x] = window;
    shared_clip_code[LocalInvocationID.y][LocalInvocationID.x] = uint8_t(clip_code);

    barrier();

    uint code_a = shared_clip_code[LocalInvocationID.y][prim.x];
    uint code_b = shared_clip_code[LocalInvocationID.y][prim.y];
    uint code_c = shared_clip_code[LocalInvocationID.y][prim.z];
#endif

    uint or_code = code_a | code_b | code_c;
    uint and_code = code_a & code_b & code_c;

    bool culled_planes = (and_code & CLIP_CODE_PLANES) != 0;
    bool is_active_prim = false;

    if (!culled_planes)
    {
        is_active_prim = (or_code & (CLIP_CODE_INACCURATE | CLIP_CODE_NEGATIVE_W)) != 0;

        if (!is_active_prim)
        {
#if CULL_MODE == CULL_MODE_GENERIC
            vec2 window_a = shared_window_positions[LocalInvocationID.y][prim.x];
            vec2 window_b = shared_window_positions[LocalInvocationID.y][prim.y];
            vec2 window_c = shared_window_positions[LocalInvocationID.y][prim.z];
#endif
            is_active_prim = cull_triangle(window_a, window_b, window_c);
        }
    }

	uint vert_mask = 0u;
	if (is_active_prim)
		vert_mask = (1u << prim.x) | (1u << prim.y) | (1u << prim.z);

#if CULL_MODE != CULL_MODE_GENERIC
	uvec4 prim_ballot = subgroupBallot(is_active_prim);
	shared_active_prim_offset = subgroupBallotExclusiveBitCount(prim_ballot);
	shared_active_vert_mask = subgroupOr(vert_mask);
#endif

#if CULL_MODE == CULL_MODE_WG32
	shared_active_prim_count_total = subgroupBallotBitCount(prim_ballot);
	shared_active_vert_count_total = bitCount(shared_active_vert_mask);
#elif CULL_MODE == CULL_MODE_WAVE32
	if (subgroupElect())
	{
		shared_active_prim_count[gl_SubgroupID] = subgroupBallotBitCount(prim_ballot);
		shared_active_vert_count[gl_SubgroupID] = bitCount(shared_active_vert_mask);
	}
#else
	if (is_active_prim)
	{
		atomicOr(shared_active_prim_mask[LocalInvocationID.y], 1u << LocalInvocationID.x);
		atomicOr(shared_active_vert_mask[LocalInvocationID.y], vert_mask);
	}
#endif

#if CULL_MODE != CULL_MODE_WG32
    barrier();

    if (gl_LocalInvocationIndex == 0)
    {
        uint active_prim = 0;
        uint active_vert = 0;

        for (uint i = 0; i < gl_WorkGroupSize.y; i++)
        {
#if CULL_MODE == CULL_MODE_WAVE32
            uint prim_count = shared_active_prim_count[i];
            uint vert_count = shared_active_vert_count[i];
#else
            uint prim_count = bitCount(shared_active_prim_mask[i]);
            uint vert_count = bitCount(shared_active_vert_mask[i]);
#endif
            shared_active_prim_count[i] = active_prim;
            shared_active_vert_count[i] = active_vert;
            active_prim += prim_count;
            active_vert += vert_count;
        }

        shared_active_prim_count_total = active_prim;
        shared_active_vert_count_total = active_vert;
    }

    barrier();
#endif

    SetMeshOutputsEXT(shared_active_vert_count_total, shared_active_prim_count_total);

    if (is_active_prim)
    {
#ifdef MESHLET_PRIMITIVE_CULL_SHARED_INDEX
	    MESHLET_PRIMITIVE_CULL_SHARED_INDEX[compacted_index_output()] = pack32(u8vec4(remap_index_buffer(prim), 0));
#else
	    gl_PrimitiveTriangleIndicesEXT[compacted_index_output()] = remap_index_buffer(prim);
#endif
    }
}

#endif
