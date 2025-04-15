set -xe

. $(dirname "$0")/common.sh

run_test() {
    export MMTK_PLAN=MarkSweep

    # Malloc marksweep is horribly slow. We just run fop.
    runbms_dacapo2006_with_heap_multiplier fop 4 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers

    unset MMTK_PLAN
}

unset JAVA_TOOL_OPTIONS
unset MMTK_PLAN

# --- Normal test ---
run_test
