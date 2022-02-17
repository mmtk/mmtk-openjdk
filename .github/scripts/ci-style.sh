set -xe

. $(dirname "$0")/common.sh

export RUSTFLAGS="-D warnings"

pushd $BINDGING_PATH/mmtk
cargo clippy
cargo clippy --release

cargo fmt -- --check
popd

find $BINDGING_PATH/openjdk \
    $BINDGING_PATH/mmtk \
    -name '*.hpp' \
    -o -name '*.cpp' \
    -o -name '*.rs' \
    -o -name '*.toml' \
    -o -name '*.gmk' \
    -exec ./ci-check-lineends.sh '{}' \;
