BINDING_PATH=$(realpath $(dirname "$0"))/../..
OPENJDK_PATH=$BINDING_PATH/repos/openjdk
DACAPO_PATH=$OPENJDK_PATH/dacapo

RUSTUP_TOOLCHAIN=`cat $BINDING_PATH/mmtk/rust-toolchain`
