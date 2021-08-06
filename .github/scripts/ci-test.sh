set -ex

cur=$(realpath $(dirname "$0"))
cd $cur
./ci-test-normal.sh
cd $cur
./ci-test-assertions.sh
