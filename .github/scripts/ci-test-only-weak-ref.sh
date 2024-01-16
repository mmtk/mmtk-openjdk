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
    # The test may fail. Skip it for now.
    #/home/runner/work/mmtk-openjdk/mmtk-openjdk/bundles/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms92M -Xmx92M -jar /home/runner/work/mmtk-openjdk/mmtk-openjdk/dacapo/dacapo-2006-10-MR2.jar eclipse
    #[2024-01-15T04:42:55Z INFO  mmtk::memory_manager] Initialized MMTk with Immix (FixedHeapSize(96468992))
    #===== DaCapo eclipse starting =====
    #[2024-01-15T04:42:58Z INFO  mmtk::util::heap::gc_trigger] [POLL] immix: Triggering collection (23560/23552 pages)
    #[2024-01-15T04:42:58Z INFO  mmtk::scheduler::gc_work] End of GC (5015/23552 pages, took 76 ms)
    #<setting up workspace...>
    #<creating projects..............................................................>
    #
    # A fatal error has been detected by the Java Runtime Environment:
    #
    #  SIGSEGV (0xb) at pc=0x00007f7dd4627dff, pid=2923, tid=2924
    #
    # JRE version: OpenJDK Runtime Environment (11.0.19) (fastdebug build 11.0.19-internal+0-adhoc.runner.openjdk)
    # Java VM: OpenJDK 64-Bit Server VM (fastdebug 11.0.19-internal+0-adhoc.runner.openjdk, mixed mode, tiered, compressed oops, third-party gc, linux-amd64)
    # Problematic frame:
    # j  java.lang.invoke.LambdaFormEditor.getInCache(Ljava/lang/invoke/LambdaFormEditor$Transform;)Ljava/lang/invoke/LambdaForm;+175 java.base@11.0.19-internal
    #
    # runbms_dacapo2006_with_heap_multiplier eclipse $heap_multiplier
}

# --- Immix ---
export MMTK_PLAN=Immix

run_all 4

# --- MarkCompact ---
export MMTK_PLAN=MarkCompact

run_all 4
