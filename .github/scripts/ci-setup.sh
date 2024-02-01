set -xe

. $(dirname "$0")/common.sh

# Install nightly rust
rustup toolchain install $RUSTUP_TOOLCHAIN
rustup target add i686-unknown-linux-gnu --toolchain $RUSTUP_TOOLCHAIN
rustup component add clippy --toolchain $RUSTUP_TOOLCHAIN
rustup component add rustfmt --toolchain $RUSTUP_TOOLCHAIN
rustup override set $RUSTUP_TOOLCHAIN

# Install running
pip3 install running-ng

# Install dependencies
sudo apt-get update -y
sudo apt-get install dos2unix
sudo apt-get install build-essential libx11-dev libxext-dev libxrender-dev libxtst-dev libxt-dev libcups2-dev libasound2-dev libxrandr-dev
