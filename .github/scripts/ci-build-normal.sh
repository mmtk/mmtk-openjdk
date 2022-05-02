set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS

# To OpenJDK folder
cd $OPENJDK_PATH

# Choose build: use slowdebug for shorter build time (32m user time for release vs. 20m user time for slowdebug)
export DEBUG_LEVEL=fastdebug

# Build
sh configure --disable-warnings-as-errors --with-debug-level=$DEBUG_LEVEL
make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$BINDING_PATH/openjdk
