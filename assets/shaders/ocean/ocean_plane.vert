layout(location = 0) in vec2 aPosition;

void main()
{
    vec2 integer_pos = aPosition + registers.coord_offset;
    vec2 uv = integer_pos * registers.inv_heightmap_size;
    vec2 centered_uv = uv + 0.5 * registers.inv_heightmap_size;
    vGradNormalUV = vec4(centered_uv, centered_uv * registers.normal_uv_scale);

    vec2 world_pos = integer_pos * registers.integer_to_world_mod;
    vec3 world = vec3(world_pos.x, 0.0, world_pos.y) + registers.world_offset;
    vPos = world;
    gl_Position = global.view_projection * vec4(world, 1.0);
}
