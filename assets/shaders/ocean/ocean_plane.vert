layout(location = 0) in vec2 aPosition;

void main()
{
    vec2 pos = aPosition + registers.base_position;
    vec2 uv = pos * registers.inv_heightmap_size;
    vec2 centered_uv = uv + 0.5 * registers.inv_heightmap_size;
    vGradNormalUV = vec4(centered_uv, centered_uv * registers.normal_uv_scale);

    pos *= registers.integer_to_world_mod;
    vec3 world = vec3(pos.x, 0.0, pos.y);
    vPos = world;
    gl_Position = global.view_projection * vec4(world, 1.0);
}
