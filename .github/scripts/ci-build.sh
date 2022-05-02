set -xe

. $(dirname "$0")/common.sh

unset JAVA_TOOL_OPTIONS
unset MMTK_PLAN

# To OpenJDK folder
cd $OPENJDK_PATH

# Build
sh configure --disable-warnings-as-errors --with-debug-level=$DEBUG_LEVEL
make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$BINDING_PATH/openjdk
