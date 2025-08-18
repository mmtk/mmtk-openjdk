set -ex

. $(dirname "$0")/common.sh
cur=$BINDING_PATH/.github/scripts

# This script is only used by MMTk core.
# OPENJDK_PATH is the default path set in ci-checkout.sh
export OPENJDK_PATH=$BINDING_PATH/repos/openjdk
export DEBUG_LEVEL=fastdebug
export TEST_JAVA_BIN=$OPENJDK_PATH/build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java

# Download dacapo
export DACAPO_PATH=$BINDING_PATH/dacapo
mkdir -p $DACAPO_PATH
wget https://downloads.sourceforge.net/project/dacapobench/archive/2006-10-MR2/dacapo-2006-10-MR2.jar -O $DACAPO_PATH/dacapo-2006-10-MR2.jar

# Normal build
$cur/ci-build.sh
# Test
$cur/ci-test-only-normal.sh
$cur/ci-test-only-normal-no-compressed-oops.sh
$cur/ci-test-only-weak-ref.sh

# Build with extreme assertions
MMTK_EXTREME_ASSERTIONS=1 $cur/ci-build.sh
$cur/ci-test-assertions.sh

# Build with vo bit
MMTK_VO_BIT=1 $cur/ci-build.sh
$cur/ci-test-vo-bit.sh

# Build with malloc mark sweep
MMTK_EXTREME_ASSERTIONS=1 MMTK_MALLOC_MARK_SWEEP=1 $cur/ci-build.sh
$cur/ci-test-malloc-mark-sweep.sh

# Build with sanity
MMTK_SANITY=1 $cur/ci-build.sh
$cur/ci-test-sanity.sh

# Build with mark in header - comment this out as it takes too long.
# export MMTK_MARK_IN_HEADER=1
# export MMTK_MALLOC_MARK_SWEEP=1
# $cur/ci-build.sh
# $cur/ci-test-malloc-mark-sweep.sh
# unset MMTK_MARK_IN_HEADER
# unset MMTK_MALLOC_MARK_SWEEP
