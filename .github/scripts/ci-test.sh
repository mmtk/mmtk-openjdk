set -ex

cur=$(realpath $(dirname "$0"))
cd $cur
./ci-build.sh
cd $cur
./ci-test-only-normal.sh
cd $cur
./ci-test-only-weak-ref.sh
cd $cur
./ci-test-assertions.sh
cd $cur
./ci-test-global-alloc-bit.sh
cd $cur
./ci-test-header-mark-bit.sh
