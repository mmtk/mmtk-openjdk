set -ex

. $(dirname "$0")/common-binding-test.sh

# Normal build
$cur/ci-build.sh

# Test
$cur/ci-test-only-normal.sh
$cur/ci-test-only-normal-no-compressed-oops.sh

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
