project_root=$(dirname "$0")/../..
set -xe

export RUSTFLAGS="-D warnings"

cd $project_root/mmtk
cargo clippy
cargo clippy --release

cargo fmt -- --check