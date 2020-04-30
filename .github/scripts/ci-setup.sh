set -xe

export RUST_VERSION=nightly-2019-08-26

# Install nightly rust
rustup toolchain install $RUST_VERSION
rustup target add i686-unknown-linux-gnu --toolchain $RUST_VERSION
rustup override set $RUST_VERSION

# Download dacapo
mkdir -p repos/openjdk/benchmarks
wget https://downloads.sourceforge.net/project/dacapobench/archive/2006-10-MR2/dacapo-2006-10-MR2.jar -O repos/openjdk/benchmarks/dacapo-2006-10-MR2.jar
