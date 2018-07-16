#ifndef OCEAN_INC_H_
#define OCEAN_INC_H_

struct PatchData
{
    vec2 Offsets;
    float InnerLOD;
    float Padding;
    vec4 LODs;
};

struct IndirectDraw
{
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
    uint padding0;
    uint padding1;
    uint padding2;
};

#endif