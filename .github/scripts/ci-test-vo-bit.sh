<<<<<<< HEAD
set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS

export MMTK_EXTREME_ASSERTIONS=0
export VO_BIT=1
. $(dirname "$0")/ci-build.sh

cd $OPENJDK_PATH

# --- SemiSpace ---
export MMTK_PLAN=SemiSpace

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar luindex


# --- Immix ---
export MMTK_PLAN=Immix

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar luindex

# --- GenImmix ---
export MMTK_PLAN=GenImmix

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar luindex

# --- StickyImmix ---
export MMTK_PLAN=StickyImmix

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar luindex

# --- GenCopy ---
export MMTK_PLAN=GenCopy

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar luindex

# --- MarkSweep ---
export MMTK_PLAN=MarkSweep

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar luindex

# --- NoGC ---

export MMTK_PLAN=NoGC

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms1G -Xmx1G -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms1G -Xmx1G -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms1G -Xmx1G -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar luindex
||||||| bbdc161
=======
set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS

run_subset() {
    heap_multiplier=$1

    runbms_dacapo2006_with_heap_multiplier antlr $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier fop $heap_multiplier
    runbms_dacapo2006_with_heap_multiplier luindex $heap_multiplier
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

# --- NoGC ---

export MMTK_PLAN=NoGC

runbms_dacapo2006_with_heap_size antlr 1000 1000
runbms_dacapo2006_with_heap_size fop 1000 1000
runbms_dacapo2006_with_heap_size luindex 1000 1000
>>>>>>> master
