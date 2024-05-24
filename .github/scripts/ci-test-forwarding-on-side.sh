set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS

run_subset() {
    heap_multiplier=$1

    runbms_dacapo2006_with_heap_multiplier antlr $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier fop $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier luindex $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier lusearch $heap_multiplier
}

# Run plans that involve CopySpace and ImmixSpace which use forwarding bits.
MMTK_PLAN=SemiSpace run_subset 4
MMTK_PLAN=Immix run_subset 4
MMTK_PLAN=GenCopy run_subset 4
MMTK_PLAN=GenImmix run_subset 4
MMTK_PLAN=StickyImmix run_subset 4
