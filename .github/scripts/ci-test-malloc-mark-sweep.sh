set -xe

. $(dirname "$0")/common.sh

build() {
    cd $OPENJDK_PATH
    export MMTK_MALLOC_MARK_SWEEP=1
    sh configure --disable-warnings-as-errors --with-debug-level=$DEBUG_LEVEL
    make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$BINDING_PATH/openjdk
    unset MMTK_MALLOC_MARK_SWEEP
}

run_test() {
    export MMTK_PLAN=MarkSweep

    # Malloc marksweep is horribly slow. We just run fop.

    # build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar antlr
    build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:-UseCompressedOops -XX:-UseCompressedClassPointers -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms50M -Xmx50M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar fop
    # build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar luindex
    # build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar pmd
    # build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar hsqldb
    # build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar eclipse

    unset MMTK_PLAN
}

unset JAVA_TOOL_OPTIONS
unset MMTK_PLAN

# --- Normal test ---
build
run_test

# --- Header mark bit ---
export MARK_IN_HEADER=1
build
run_test
unset MARK_IN_HEADER

# --- Test assertions ---
export MMTK_EXTREME_ASSERTIONS=1
build
run_test
unset MMTK_EXTREME_ASSERTIONS
