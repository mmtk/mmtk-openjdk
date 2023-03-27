BINDING_PATH=$(realpath $(dirname "$0"))/../..
OPENJDK_PATH=$BINDING_PATH/../openjdk
DACAPO_PATH=$BINDING_PATH/../dacapo
RUSTUP_TOOLCHAIN=`cat $BINDING_PATH/mmtk/rust-toolchain`
