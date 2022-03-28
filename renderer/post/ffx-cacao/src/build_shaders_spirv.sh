#!/bin/bash

if [ -z $DXC ]; then
	echo "DXC not set, assuming it is in PATH."
	DXC=./cauldron-dxc-wrapper.sh
fi

DXC_32="$DXC -Wno-conversion -spirv -T cs_6_2 -fspv-target-env=vulkan1.1"

OUTDIR=../../../../assets/shaders/post/ffx-cacao
mkdir -p $OUTDIR

compile_dxc() {
	echo "Compiling $1"
	$DXC_32 -Fo $OUTDIR/tmp.spv -E $2 ffx_cacao.hlsl \
		-fvk-bind-register s0 0 0 0 \
		-fvk-bind-register s1 0 1 0 \
		-fvk-bind-register s2 0 2 0 \
		-fvk-bind-register s3 0 3 0 \
		-fvk-bind-register s4 0 4 0 \
		-fvk-bind-register b0 0 0 1 \
		-fvk-bind-register t0 0 0 2 \
		-fvk-bind-register t1 0 1 2 \
		-fvk-bind-register t2 0 2 2 \
		-fvk-bind-register t3 0 3 2 \
		-fvk-bind-register t4 0 4 2 \
		-fvk-bind-register t5 0 5 2 \
		-fvk-bind-register t6 0 6 2 \
		-fvk-bind-register t7 0 7 2 \
		-fvk-bind-register u0 0 8 2 \
		-fvk-bind-register u1 0 9 2 \
		-fvk-bind-register u2 0 10 2 \
		-fvk-bind-register u3 0 11 2 \
		-fvk-bind-register u4 0 12 2 \
		-fvk-bind-register u5 0 13 2 \
		-fvk-bind-register u6 0 14 2 \
		-fvk-bind-register u7 0 15 2

	echo "Patching entry point for $1"
	spirv-dis -o $OUTDIR/tmp.asm $OUTDIR/tmp.spv
	# DXC emits OpEntryPoint with non-main. Patch it back to main and reassemble, sigh.
	sed -i $OUTDIR/tmp.asm -e "s/$2/main/g"
	spirv-as -o $OUTDIR/tmp.spv $OUTDIR/tmp.asm --target-env vulkan1.1
	echo "Validating $1"
	spirv-val $OUTDIR/tmp.spv --target-env vulkan1.1
	echo "Success for $1"
	mv $OUTDIR/tmp.spv $OUTDIR/$1.spv
	rm $OUTDIR/tmp.asm
}

# Only run once, so that we can generate SPIR-V and bake it in.

compile_dxc CACAOClearLoadCounter_32                            FFX_CACAO_ClearLoadCounter
compile_dxc CACAOPrepareDownsampledDepths_32                    FFX_CACAO_PrepareDownsampledDepths
compile_dxc CACAOPrepareNativeDepths_32                         FFX_CACAO_PrepareNativeDepths
compile_dxc CACAOPrepareDownsampledDepthsAndMips_32             FFX_CACAO_PrepareDownsampledDepthsAndMips
compile_dxc CACAOPrepareNativeDepthsAndMips_32                  FFX_CACAO_PrepareNativeDepthsAndMips
compile_dxc CACAOPrepareDownsampledNormals_32                   FFX_CACAO_PrepareDownsampledNormals
compile_dxc CACAOPrepareNativeNormals_32                        FFX_CACAO_PrepareNativeNormals
compile_dxc CACAOPrepareDownsampledNormalsFromInputNormals_32   FFX_CACAO_PrepareDownsampledNormalsFromInputNormals
compile_dxc CACAOPrepareNativeNormalsFromInputNormals_32        FFX_CACAO_PrepareNativeNormalsFromInputNormals
compile_dxc CACAOPrepareDownsampledDepthsHalf_32                FFX_CACAO_PrepareDownsampledDepthsHalf
compile_dxc CACAOPrepareNativeDepthsHalf_32                     FFX_CACAO_PrepareNativeDepthsHalf
compile_dxc CACAOGenerateQ0_32                                  FFX_CACAO_GenerateQ0
compile_dxc CACAOGenerateQ1_32                                  FFX_CACAO_GenerateQ1
compile_dxc CACAOGenerateQ2_32                                  FFX_CACAO_GenerateQ2
compile_dxc CACAOGenerateQ3_32                                  FFX_CACAO_GenerateQ3
compile_dxc CACAOGenerateQ3Base_32                              FFX_CACAO_GenerateQ3Base
compile_dxc CACAOGenerateImportanceMap_32                       FFX_CACAO_GenerateImportanceMap
compile_dxc CACAOPostprocessImportanceMapA_32                   FFX_CACAO_PostprocessImportanceMapA
compile_dxc CACAOPostprocessImportanceMapB_32                   FFX_CACAO_PostprocessImportanceMapB
compile_dxc CACAOEdgeSensitiveBlur1_32                          FFX_CACAO_EdgeSensitiveBlur1
compile_dxc CACAOEdgeSensitiveBlur2_32                          FFX_CACAO_EdgeSensitiveBlur2
compile_dxc CACAOEdgeSensitiveBlur3_32                          FFX_CACAO_EdgeSensitiveBlur3
compile_dxc CACAOEdgeSensitiveBlur4_32                          FFX_CACAO_EdgeSensitiveBlur4
compile_dxc CACAOEdgeSensitiveBlur5_32                          FFX_CACAO_EdgeSensitiveBlur5
compile_dxc CACAOEdgeSensitiveBlur6_32                          FFX_CACAO_EdgeSensitiveBlur6
compile_dxc CACAOEdgeSensitiveBlur7_32                          FFX_CACAO_EdgeSensitiveBlur7
compile_dxc CACAOEdgeSensitiveBlur8_32                          FFX_CACAO_EdgeSensitiveBlur8
compile_dxc CACAOApply_32                                       FFX_CACAO_Apply
compile_dxc CACAONonSmartApply_32                               FFX_CACAO_NonSmartApply
compile_dxc CACAONonSmartHalfApply_32                           FFX_CACAO_NonSmartHalfApply
compile_dxc CACAOUpscaleBilateral5x5Smart_32                    FFX_CACAO_UpscaleBilateral5x5Smart
compile_dxc CACAOUpscaleBilateral5x5NonSmart_32                 FFX_CACAO_UpscaleBilateral5x5NonSmart
compile_dxc CACAOUpscaleBilateral5x5Half_32                     FFX_CACAO_UpscaleBilateral5x5Half

