set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS

export MMTK_EXTREME_ASSERTIONS=1
. $(dirname "$0")/ci-build.sh
export TEST_JAVA_BIN=$OPENJDK_PATH/build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java

cd $OPENJDK_PATH

run_subset() {
    heap_multiplier=$1

    runbms_dacapo2006_with_heap_multiplier fop $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier luindex $heap_multiplier
}

# -- SemiSpace --
export MMTK_PLAN=SemiSpace

run_subset 2

# --- Immix ---
export MMTK_PLAN=Immix

run_subset 2

# --- GenImmix ---
export MMTK_PLAN=GenImmix

run_subset 2

# --- StickyImmix ---
export MMTK_PLAN=StickyImmix

run_subset 2

# -- GenCopy --
export MMTK_PLAN=GenCopy

run_subset 2

# -- NoGC --
export MMTK_PLAN=NoGC

runbms_dacapo2006_with_heap_size fop 1000 1000
runbms_dacapo2006_with_heap_size luindex 1000 1000

# --- MarkSweep ---
export MMTK_PLAN=MarkSweep

run_subset 2

# -- PageProtect --
sudo sysctl -w vm.max_map_count=655300
export MMTK_PLAN=PageProtect

# Note: Disable compressed pointers as it does not work well with GC plans that uses virtual memory excessively.
runbms_dacapo2006_with_heap_size fop 4000 4000 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
runbms_dacapo2006_with_heap_size luindex 4000 4000 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers
