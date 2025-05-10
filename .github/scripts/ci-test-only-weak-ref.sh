set -xe

# Just run everything in ci-test-only-normal.sh, but with reference processing enabled.
export MMTK_NO_REFERENCE_TYPES=false
. $(dirname "$0")/ci-test-only-normal.sh
