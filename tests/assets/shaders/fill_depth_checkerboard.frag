#version 450
void main()
{
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    gl_FragDepth = 0.45 + 0.1 * float((x ^ y) & 1);
}
