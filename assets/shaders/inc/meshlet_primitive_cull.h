#ifndef MESHLET_PRIMITIVE_CULL_H_
#define MESHLET_PRIMITIVE_CULL_H_

#ifndef MESHLET_SIZE
#error "Must define MESHLET_SIZE"
#endif

shared vec2 shared_window_positions[gl_WorkGroupSize.y][gl_WorkGroupSize.x];
shared uint8_t shared_clip_code[gl_WorkGroupSize.y][gl_WorkGroupSize.x];
shared uint shared_active_vert[gl_WorkGroupSize.y];
shared uint shared_active_prim[gl_WorkGroupSize.y];
shared uint shared_active_vert_count[gl_WorkGroupSize.y];
shared uint shared_active_prim_count[gl_WorkGroupSize.y];
shared uint shared_active_vert_count_total;
shared uint shared_active_prim_count_total;

const uint CLIP_CODE_INACCURATE = 1 << 0;
const uint CLIP_CODE_NEGATIVE_W = 1 << 1;
const uint CLIP_CODE_NEGATIVE_X = 1 << 2;
const uint CLIP_CODE_NEGATIVE_Y = 1 << 3;
const uint CLIP_CODE_POSITIVE_X = 1 << 4;
const uint CLIP_CODE_POSITIVE_Y = 1 << 5;
const uint CLIP_CODE_PLANES = uint(-1) & ~CLIP_CODE_INACCURATE;

uvec2 LocalInvocationID;

void meshlet_setup_local_invocation(uvec2 local_id)
{
    LocalInvocationID = local_id;
}

uint compacted_vertex_output(uint index)
{
    return shared_active_vert_count[LocalInvocationID.y] +
        bitCount(bitfieldExtract(shared_active_vert[LocalInvocationID.y], 0, int(index)));
}

uint meshlet_compacted_vertex_output()
{
    return compacted_vertex_output(LocalInvocationID.x);
}

uint compacted_index_output()
{
    return shared_active_prim_count[LocalInvocationID.y] +
        bitCount(bitfieldExtract(shared_active_prim[LocalInvocationID.y], 0, int(LocalInvocationID.x)));
}

bool meshlet_lane_has_active_vert()
{
    return (shared_active_vert[LocalInvocationID.y] & (1u << LocalInvocationID.x)) != 0u;
}

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

void meshlet_init_shared()
{
    if (gl_LocalInvocationIndex < MESHLET_SIZE / 32)
    {
        shared_active_vert[gl_LocalInvocationIndex] = 0;
        shared_active_prim[gl_LocalInvocationIndex] = 0;
    }
}

uint meshlet_get_meshlet_index()
{
#if MESHLET_SIZE != 256
    return gl_WorkGroupID.y;
#else
    return gl_WorkGroupID.x;
#endif
}

uint meshlet_get_base_chunk_index()
{
#if MESHLET_SIZE != 256
    return gl_WorkGroupID.x * (MESHLET_SIZE / 32);
#else
    return 0;
#endif
}

void meshlet_emit_clip_pos(vec4 clip_pos, vec4 viewport)
{
    meshlet_init_shared();
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

    barrier();
}

void meshlet_emit_primitive(uvec3 prim)
{
    uint code_a = shared_clip_code[LocalInvocationID.y][prim.x];
    uint code_b = shared_clip_code[LocalInvocationID.y][prim.y];
    uint code_c = shared_clip_code[LocalInvocationID.y][prim.z];

    uint or_code = code_a | code_b | code_c;
    uint and_code = code_a & code_b & code_c;

    bool culled_planes = (and_code & CLIP_CODE_PLANES) != 0;
    bool is_active_prim = false;

    if (!culled_planes)
    {
        bool force_accept = (or_code & (CLIP_CODE_INACCURATE | CLIP_CODE_NEGATIVE_W)) != 0;

        if (!force_accept)
        {
            vec2 a = shared_window_positions[LocalInvocationID.y][prim.x];
            vec2 b = shared_window_positions[LocalInvocationID.y][prim.y];
            vec2 c = shared_window_positions[LocalInvocationID.y][prim.z];
            force_accept = cull_triangle(a, b, c);
        }

        if (force_accept)
        {
            is_active_prim = true;
            atomicOr(shared_active_prim[LocalInvocationID.y], 1u << LocalInvocationID.x);
            atomicOr(shared_active_vert[LocalInvocationID.y], 1u << prim.x);
            atomicOr(shared_active_vert[LocalInvocationID.y], 1u << prim.y);
            atomicOr(shared_active_vert[LocalInvocationID.y], 1u << prim.z);
        }
    }

    barrier();

    if (gl_LocalInvocationIndex == 0)
    {
        uint active_prim = 0;
        uint active_vert = 0;

        for (uint i = 0; i < gl_WorkGroupSize.y; i++)
        {
            shared_active_prim_count[i] = active_prim;
            shared_active_vert_count[i] = active_vert;
            active_prim += bitCount(shared_active_prim[i]);
            active_vert += bitCount(shared_active_vert[i]);
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
