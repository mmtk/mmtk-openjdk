set -ex

. $(dirname "$0")/common-binding-test.sh

# Normal build
$cur/ci-build.sh

# Test
MMTK_PLAN=SemiSpace runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=Immix runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=StickyImmix runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=GenCopy runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=MarkCompact runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=Compressor runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=MarkSweep runbms_dacapo2006_with_heap_multiplier fop 8
MMTK_PLAN=NoGC runbms_dacapo2006_with_heap_size fop 1000 1000
# Test heap resizing
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_size fop 20 100
# Test compressed oops with heap range > 4GB
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_size fop 20 5000
# Test no compressed oop
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_multiplier fop 4 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers

# Build with vo bit
MMTK_VO_BIT=1 $cur/ci-build.sh
# Test
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_multiplier fop 4

# Build with on-the-side forwarding bits
MMTK_FORWARDING_ON_SIDE=1 $cur/ci-build.sh
# Test
MMTK_PLAN=SemiSpace runbms_dacapo2006_with_heap_multiplier lusearch 4
MMTK_PLAN=Immix runbms_dacapo2006_with_heap_multiplier lusearch 4
MMTK_PLAN=GenCopy runbms_dacapo2006_with_heap_multiplier lusearch 4
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_multiplier lusearch 4
MMTK_PLAN=StickyImmix runbms_dacapo2006_with_heap_multiplier lusearch 4
