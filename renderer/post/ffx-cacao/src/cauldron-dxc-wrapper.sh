#!/bin/bash

if [ -z $FFX_CACAO ]; then
	FFX_CACAO=$HOME/git/FidelityFX-CACAO
	echo "FFX_CACAO not set, assuming checkout is in $FFX_CACAO."
fi

# Upstream DXC fails to compile to SPIR-V due to invalid use of texture offsets.

wine $FFX_CACAO/sample/libs/cauldron/libs/DXC/bin/dxc.exe "$@"
