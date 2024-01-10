set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS
cd $OPENJDK_PATH

export MMTK_NO_REFERENCE_TYPES=false
# Just test Immix and MarkCompact
# Immix - normal weak ref impl
# MarkCompact - with extra ref forwarding

run_all() {
    heap_multiplier=$1

    runbms_dacapo2006_with_heap_multiplier antlr $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier fop $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier luindex $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier pmd $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier hsqldb $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier eclipse $heap_multiplier
}

# --- Immix ---
export MMTK_PLAN=Immix

run_all 4

# --- MarkCompact ---
export MMTK_PLAN=MarkCompact

run_all 4
