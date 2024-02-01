set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS

run_all() {
    heap_multiplier=$1

    runbms_dacapo2006_with_heap_multiplier antlr $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier fop $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier luindex $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier pmd $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier hsqldb $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier eclipse $heap_multiplier
}

# --- SemiSpace ---
export MMTK_PLAN=SemiSpace

run_all 4

# Test heap resizing
runbms_dacapo2006_with_heap_size fop 20 100

# --- Immix ---
export MMTK_PLAN=Immix

run_all 4

# Test heap resizing
runbms_dacapo2006_with_heap_size fop 20 100

# --- GenImmix ---
export MMTK_PLAN=GenImmix

run_all 4

# Test heap resizing
runbms_dacapo2006_with_heap_size fop 20 100

# --- StickyImmix ---
export MMTK_PLAN=StickyImmix

run_all 4

# --- GenCopy ---
export MMTK_PLAN=GenCopy

run_all 4

# --- NoGC ---
export MMTK_PLAN=NoGC

runbms_dacapo2006_with_heap_size antlr 1000 1000
runbms_dacapo2006_with_heap_size fop 1000 1000
runbms_dacapo2006_with_heap_size luindex 1000 1000

# --- MarkCompact ---
export MMTK_PLAN=MarkCompact

run_all 4

# --- MarkSweep ---
export MMTK_PLAN=MarkSweep

run_all 8
