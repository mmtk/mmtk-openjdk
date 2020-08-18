set -xe

# Install nightly rust
rustup toolchain install $RUSTUP_TOOLCHAIN
rustup target add i686-unknown-linux-gnu --toolchain $RUSTUP_TOOLCHAIN
rustup override set $RUSTUP_TOOLCHAIN

# Download dacapo
mkdir -p repos/openjdk/benchmarks
wget https://downloads.sourceforge.net/project/dacapobench/archive/2006-10-MR2/dacapo-2006-10-MR2.jar -O repos/openjdk/benchmarks/dacapo-2006-10-MR2.jar

# Install dependencies
sudo apt-get update -y
sudo apt-get install build-essential libx11-dev libxext-dev libxrender-dev libxtst-dev libxt-dev libcups2-dev libasound2-dev
