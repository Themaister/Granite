#!/bin/sh

prims=100
frames=100

export radv_dgc=true
prims_cmd="--primitives-per-draw $prims"

for SEQ in 16 32 64 128 256 512 1024 2048 4096 8192
do
	echo "==== Sequence count $SEQ tests ===="
	# Direct count
	echo "=== NV_dgc direct count || $prims triangles per draw || direct count = $SEQ || dispatches = 10 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-graphics --dgc --iterations 10 --max-count $SEQ --indirect $prims_cmd --frames $frames 2>/dev/null
	# Indirect count with no empty subdraws.
	echo "=== NV_dgc indirect count || $prims triangles per draw || indirect count = $SEQ || dispatches = 10 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-graphics --dgc --iterations 10 --indirect-count $SEQ --max-count $SEQ --indirect $prims_cmd --frames $frames 2>/dev/null
	# Indirect count with mostly empty subdraws.
	echo "=== NV_dgc indirect count || $prims triangles per draw || indirect count = 4 || dispatches = 250 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-graphics --dgc --iterations 250 --indirect-count 4 --max-count $SEQ --indirect $prims_cmd --frames $frames 2>/dev/null
	# Extremely sparse.
	echo "=== NV_dgc indirect count || $prims triangles per draw || indirect count = 1 || dispatches = 1000 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-graphics --dgc --iterations 1000 --indirect-count 1 --max-count $SEQ --indirect $prims_cmd --frames $frames 2>/dev/null
	echo "==========================="
	echo ""
done

echo "==== Direct tests ===="
echo "=== Direct || $prims triangles per draw || 1000 draws per frame ==="
./tests/dgc-test-graphics --iterations 1000 $prims_cmd --frames $frames 2>/dev/null
echo "=== Direct || $prims triangles per draw || 2000 draws per frame ==="
./tests/dgc-test-graphics --iterations 2000 $prims_cmd --frames $frames 2>/dev/null
echo "==============="
echo ""

echo "==== Indirect tests ===="
echo "=== Indirect || $prims triangles per draw || 1000 unrolled indirect draws per frame ==="
./tests/dgc-test-graphics --iterations 10 --max-count 100 --indirect $prims_cmd --frames $frames 2>/dev/null
echo "=== Indirect || $prims triangles per draw || 2000 unrolled indirect draws per frame ==="
./tests/dgc-test-graphics --iterations 10 --max-count 200 --indirect $prims_cmd --frames $frames 2>/dev/null
echo "==============="
echo ""

echo "==== Multi Draw Indirect tests ===="
echo "=== MDI || $prims triangles per draw || 1000 indirect draws per frame ==="
./tests/dgc-test-graphics --mdi --iterations 10 --max-count 100 --indirect $prims_cmd --frames $frames 2>/dev/null
echo "=== MDI || $prims triangles per draw || 2000 indirect draws per frame ==="
./tests/dgc-test-graphics --mdi --iterations 10 --max-count 200 --indirect $prims_cmd --frames $frames 2>/dev/null
echo "==============="
echo ""

echo "==== Multi Draw Indirect Count tests ===="
echo "=== MDI indirect count || $prims triangles per draw || 1000 indirect draws per frame ==="
./tests/dgc-test-graphics --mdi --iterations 100 --indirect-count 10 --max-count 8192 --indirect $prims_cmd --frames $frames 2>/dev/null
echo "=== MDI indirect count || $prims triangles per draw || 2000 indirect draws per frame ==="
./tests/dgc-test-graphics --mdi --iterations 100 --indirect-count 20 --max-count 8192 --indirect $prims_cmd --frames $frames 2>/dev/null
echo "==============="
echo ""
