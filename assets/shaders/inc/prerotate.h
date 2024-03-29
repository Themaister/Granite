#ifndef PREROTATE_H_
#define PREROTATE_H_

layout(constant_id =  8) const float PREROTATE_MATRIX_0 = 1.0;
layout(constant_id =  9) const float PREROTATE_MATRIX_1 = 0.0;
layout(constant_id = 10) const float PREROTATE_MATRIX_2 = 0.0;
layout(constant_id = 11) const float PREROTATE_MATRIX_3 = 1.0;

void prerotate_fixup_clip_xy()
{
	gl_Position.xy =
			mat2(PREROTATE_MATRIX_0, PREROTATE_MATRIX_1,
			     PREROTATE_MATRIX_2, PREROTATE_MATRIX_3) *
			     gl_Position.xy;
}

#endif