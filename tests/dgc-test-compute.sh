#!/bin/sh

wgs=100
frames=100

export radv_dgc=true
wgs_cmd="--dispatch $wgs"

for SEQ in 16 32 64 128 256 512 1024 2048 4096 8192
do
	echo "==== Sequence count $SEQ tests ===="
	# Direct count
	echo "=== NV_dgcc direct count || $wgs workgroups per dispatch || direct count = $SEQ || dispatches = 10 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-compute --dgcc --iterations 10 --max-count $SEQ --indirect $wgs_cmd --frames $frames 2>/dev/null
	echo "=== NV_dgcc direct count (async compute) || $wgs workgroups per dispatch || direct count = $SEQ || dispatches = 10 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-compute --dgcc --iterations 10 --max-count $SEQ --indirect $wgs_cmd --frames $frames --async 2>/dev/null
	# Indirect count with no empty subdraws.
	echo "=== NV_dgcc indirect count || $wgs workgroups per dispatch || indirect count = $SEQ || dispatches = 10 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-compute --dgcc --iterations 10 --indirect-count $SEQ --max-count $SEQ --indirect $wgs_cmd --frames $frames 2>/dev/null
	echo "=== NV_dgcc indirect count (async compute) || $wgs workgroups per dispatch || indirect count = $SEQ || dispatches = 10 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-compute --dgcc --iterations 10 --indirect-count $SEQ --max-count $SEQ --indirect $wgs_cmd --frames $frames --async 2>/dev/null
	# Indirect count with mostly empty subdraws.
	echo "=== NV_dgcc indirect count || $wgs workgroups per dispatch || indirect count = 4 || dispatches = 250 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-compute --dgcc --iterations 250 --indirect-count 4 --max-count $SEQ --indirect $wgs_cmd --frames $frames 2>/dev/null
	# Extremely sparse.
	echo "=== NV_dgcc indirect count || $wgs workgroups per dispatch || indirect count = 1 || dispatches = 1000 || maxSequenceCount $SEQ ==="
	./tests/dgc-test-compute --dgcc --iterations 1000 --indirect-count 1 --max-count $SEQ --indirect $wgs_cmd --frames $frames 2>/dev/null
	echo "==========================="
	echo ""
done

echo "==== Direct tests ===="
echo "=== Direct || $wgs workgroups per dispatch || 1000 draws per frame ==="
./tests/dgc-test-compute --iterations 1000 $wgs_cmd --frames $frames 2>/dev/null
echo "=== Direct || $wgs workgroups per dispatch || 2000 draws per frame ==="
./tests/dgc-test-compute --iterations 2000 $wgs_cmd --frames $frames 2>/dev/null
echo "==============="
echo ""

echo "==== Indirect tests ===="
echo "=== Indirect || $wgs workgroups per dispatch || 1000 unrolled indirect draws per frame ==="
./tests/dgc-test-compute --iterations 10 --max-count 100 --indirect $wgs_cmd --frames $frames 2>/dev/null
echo "=== Indirect || $wgs workgroups per dispatch || 2000 unrolled indirect draws per frame ==="
./tests/dgc-test-compute --iterations 10 --max-count 200 --indirect $wgs_cmd --frames $frames 2>/dev/null
echo "==============="
echo ""

