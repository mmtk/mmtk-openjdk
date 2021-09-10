project_root=$(dirname "$0")/../..
set -xe

export RUSTFLAGS="-D warnings"

pushd $project_root/mmtk
cargo clippy
cargo clippy --release

cargo fmt -- --check
popd

find $project_root/openjdk \
    $project_root/mmtk \
    -name '*.hpp' \
    -o -name '*.cpp' \
    -o -name '*.rs' \
    -o -name '*.toml' \
    -o -name '*.gmk' \
    -exec ./ci-check-lineends.sh '{}' \;
