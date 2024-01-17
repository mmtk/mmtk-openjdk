set -xe

. $(dirname "$0")/common.sh

ensure_env OPENJDK_PATH

# Use fastdebug if DEBUG_LEVEL is unset
DEBUG_LEVEL=${DEBUG_LEVEL:="fastdebug"}

# Build target. Could be empty, or product-bundles.
build_target=$1

# Build product bundle
cd $OPENJDK_PATH
sh configure --disable-warnings-as-errors --with-debug-level=$DEBUG_LEVEL
make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$BINDING_PATH/openjdk $OPENJDK_BUILD_TARGET
