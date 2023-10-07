#version 450

#if MDI
#extension GL_ARB_shader_draw_parameters : require
#define index gl_DrawIDARB
#else
layout(push_constant) uniform Registers { uint index; };
#endif
layout(set = 0, binding = 0) buffer SSBO { uint data[]; };

void main()
{
    atomicAdd(data[index], 1u);
    gl_Position = vec4(-1.0);
}