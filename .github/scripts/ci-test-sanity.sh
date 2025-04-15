set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS

MMTK_PLAN=Immix runbms_dacapo2006_with_heap_multiplier fop 4
