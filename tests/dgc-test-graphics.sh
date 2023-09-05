#!/bin/sh

for SEQ in 16 32 64 128 256 512 1024 2048 4096 8192
do
	echo "==== Sequence count $SEQ tests ===="
	# Direct count
	echo "=== NV_dgc direct count || 10000 triangles per draw || direct count = $SEQ || dispatches = 10 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-graphics --dgc --iterations 10 --max-count $SEQ --indirect --primitives-per-draw 10000 --frames 1000 2>/dev/null
	# Indirect count with no empty subdraws.
	echo "=== NV_dgc indirect count || 10000 triangles per draw || indirect count = $SEQ || dispatches = 10 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-graphics --dgc --iterations 10 --indirect-count $SEQ --max-count $SEQ --indirect --primitives-per-draw 10000 --frames 1000 2>/dev/null
	# Indirect count with mostly empty subdraws.
	echo "=== NV_dgc indirect count || 10000 triangles per draw || indirect count = 4 || dispatches = 250 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-graphics --dgc --iterations 250 --indirect-count 4 --max-count $SEQ --indirect --primitives-per-draw 10000 --frames 1000 2>/dev/null
	# Extremely sparse.
	echo "=== NV_dgc indirect count || 10000 triangles per draw || indirect count = 1 || dispatches = 1000 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-graphics --dgc --iterations 1000 --indirect-count 1 --max-count $SEQ --indirect --primitives-per-draw 10000 --frames 1000 2>/dev/null
	echo "==========================="
	echo ""
done

echo "==== Direct tests ===="
echo "=== Direct || 10000 triangles per draw || 1000 draws per frame ==="
./tests/dgc-test-graphics --iterations 1000 --primitives-per-draw 10000 --frames 1000 2>/dev/null
echo "=== Direct || 10000 triangles per draw || 2000 draws per frame ==="
./tests/dgc-test-graphics --iterations 2000 --primitives-per-draw 10000 --frames 1000 2>/dev/null
echo "==============="
echo ""

echo "==== Indirect tests ===="
echo "=== Indirect || 10000 triangles per draw || 1000 unrolled indirect draws per frame ==="
./tests/dgc-test-graphics --iterations 10 --max-count 100 --indirect --primitives-per-draw 10000 --frames 1000 2>/dev/null
echo "=== Indirect || 10000 triangles per draw || 2000 unrolled indirect draws per frame ==="
./tests/dgc-test-graphics --iterations 10 --max-count 200 --indirect --primitives-per-draw 10000 --frames 1000 2>/dev/null
echo "==============="
echo ""

echo "==== Multi Draw Indirect tests ===="
echo "=== MDI || 10000 triangles per draw || 1000 indirect draws per frame ==="
./tests/dgc-test-graphics --mdi --iterations 10 --max-count 100 --indirect --primitives-per-draw 10000 --frames 1000 2>/dev/null
echo "=== MDI || 10000 triangles per draw || 2000 indirect draws per frame ==="
./tests/dgc-test-graphics --mdi --iterations 10 --max-count 200 --indirect --primitives-per-draw 10000 --frames 1000 2>/dev/null
echo "==============="
echo ""

echo "==== Multi Draw Indirect Count tests ===="
echo "=== MDI indirect count || 10000 triangles per draw || 1000 indirect draws per frame ==="
./tests/dgc-test-graphics --mdi --iterations 100 --indirect-count 10 --max-count 8192 --indirect --primitives-per-draw 10000 --frames 1000 2>/dev/null
echo "=== MDI indirect count || 10000 triangles per draw || 2000 indirect draws per frame ==="
./tests/dgc-test-graphics --mdi --iterations 100 --indirect-count 20 --max-count 8192 --indirect --primitives-per-draw 10000 --frames 1000 2>/dev/null
echo "==============="
echo ""
