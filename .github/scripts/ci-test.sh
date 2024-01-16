set -ex

cur=$(realpath $(dirname "$0"))

# This script is only used by MMTk core.
# OPENJDK_PATH is the default path set in ci-checkout.sh
OPENJDK_PATH=$BINDING_PATH/repos/openjdk
export TEST_JAVA_BIN=$OPENJDK_PATH/jdk/bin/java

# Normal build
$cur/ci-build.sh
# Test
$cur/ci-test-only-normal.sh
$cur/ci-test-only-normal-no-compressed-oops.sh
$cur/ci-test-only-weak-ref.sh

# Build with extreme assertions
export MMTK_EXTREME_ASSERTIONS=1
$cur/ci-build.sh
$cur/ci-test-assertions.sh
unset MMTK_EXTREME_ASSERTIONS

# Build with vo bit
export VO_BIT=1
$cur/ci-build.sh
$cur/ci-test-vo-bit.sh
unset VO_BIT=1

# Build with malloc mark sweep
export MMTK_EXTREME_ASSERTIONS=1
export MMTK_MALLOC_MARK_SWEEP=1
$cur/ci-build.sh
$cur/ci-test-malloc-mark-sweep.sh
unset MMTK_EXTREME_ASSERTIONS
unset MMTK_MALLOC_MARK_SWEEP

# Build with mark in header
export MARK_IN_HEADER=1
export MMTK_MALLOC_MARK_SWEEP=1
$cur/ci-build.sh
$cur/ci-test-malloc-mark-sweep.sh
unset MARK_IN_HEADER
unset MMTK_MALLOC_MARK_SWEEP
