vec4 get_color()
{
   return 3.0 * subpassLoad(uDepth).xxxx;
}
