set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS

run_subset() {
    heap_multiplier=$1
    jvm_options=$2

    runbms_dacapo2006_with_heap_multiplier antlr $heap_multiplier $jvm_options
    runbms_dacapo2006_with_heap_multiplier fop $heap_multiplier $jvm_options
    runbms_dacapo2006_with_heap_multiplier luindex $heap_multiplier $jvm_options
}

# --- SemiSpace ---
export MMTK_PLAN=SemiSpace

run_subset 4

# --- Immix ---
export MMTK_PLAN=Immix

run_subset 4

# --- GenImmix ---
export MMTK_PLAN=GenImmix

run_subset 4

# --- StickyImmix ---
export MMTK_PLAN=StickyImmix

run_subset 4

# --- GenCopy ---
export MMTK_PLAN=GenCopy

run_subset 4

# --- MarkSweep ---
export MMTK_PLAN=MarkSweep

run_subset 8

# --- Compressor ---
export MMTK_PLAN=Compressor

# TODO: Need to temporarily disable compressed oops for the Compressor until it
# supports discontiguous spaces.
run_subset 4 "-XX:-UseCompressedOops -XX:-UseCompressedClassPointers"

# --- NoGC ---

export MMTK_PLAN=NoGC

runbms_dacapo2006_with_heap_size antlr 1000 1000
runbms_dacapo2006_with_heap_size fop 1000 1000
runbms_dacapo2006_with_heap_size luindex 1000 1000
