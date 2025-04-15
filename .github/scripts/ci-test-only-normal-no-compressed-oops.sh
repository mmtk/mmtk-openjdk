set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS

run_all_no_compressed_oop() {
    heap_multiplier=$2

    runbms_dacapo2006_with_heap_multiplier antlr $heap_multiplier -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
    runbms_dacapo2006_with_heap_multiplier fop $heap_multiplier -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
    runbms_dacapo2006_with_heap_multiplier luindex $heap_multiplier -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
    runbms_dacapo2006_with_heap_multiplier lusearch $heap_multiplier -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
    runbms_dacapo2006_with_heap_multiplier pmd $heap_multiplier -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
    runbms_dacapo2006_with_heap_multiplier hsqldb $heap_multiplier -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
    runbms_dacapo2006_with_heap_multiplier eclipse $heap_multiplier -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
    runbms_dacapo2006_with_heap_multiplier xalan $heap_multiplier -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
}

# --- SemiSpace ---
export MMTK_PLAN=SemiSpace

run_all_no_compressed_oop 4

# Test heap resizing
runbms_dacapo2006_with_heap_size fop 20 100 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers

# --- Immix ---
export MMTK_PLAN=Immix

run_all_no_compressed_oop 4

# Test heap resizing
runbms_dacapo2006_with_heap_size fop 20 100 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers

# --- Immix ---
export MMTK_PLAN=GenImmix

run_all_no_compressed_oop 4

# Test heap resizing
runbms_dacapo2006_with_heap_size fop 20 100 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers

# --- StickyImmix ---
export MMTK_PLAN=StickyImmix

run_all_no_compressed_oop 4

# --- GenCopy ---
export MMTK_PLAN=GenCopy

run_all_no_compressed_oop 4

# --- NoGC ---

# Build
export MMTK_PLAN=NoGC

# Test - the benchmarks that are commented out do not work yet
# Note: We could increase heap size when mmtk core can work for larger heap. We may get more benchmarks running.

runbms_dacapo2006_with_heap_size antlr 1000 1000 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
runbms_dacapo2006_with_heap_size fop 1000 1000 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
runbms_dacapo2006_with_heap_size luindex 1000 1000 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers

# --- MarkCompact ---
export MMTK_PLAN=MarkCompact

run_all_no_compressed_oop 4

# --- MarkSweep ---
export MMTK_PLAN=MarkSweep

run_all_no_compressed_oop 4

# --- PageProtect ---
# Make sure this runs last in our tests unless we want to set it back to the default limit.
sudo sysctl -w vm.max_map_count=655300

export MMTK_PLAN=PageProtect

# Note: Disable compressed pointers as it does not work well with GC plans that uses virtual memory excessively.
runbms_dacapo2006_with_heap_size antlr 4000 4000 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
runbms_dacapo2006_with_heap_size fop 4000 4000 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
runbms_dacapo2006_with_heap_size luindex 4000 4000 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
