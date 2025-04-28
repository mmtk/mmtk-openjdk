# This is the common part of binding tests (minimal and extended) invoked from mmtk-core.

set -xe

. $(dirname "$0")/common.sh
cur=$BINDING_PATH/.github/scripts

# Use fastdebug for binding tests
export DEBUG_LEVEL=fastdebug

# We build OpenJDK with the "default" target, and this is the location of the Java binary.
# It's equivalent to invoking "make" without an explicit target, but it's better to be explicit.
export OPENJDK_BUILD_TARGET=default
export TEST_JAVA_BIN=$OPENJDK_PATH/build/linux-x86_64-server-$DEBUG_LEVEL/jdk/bin/java

# Download dacapo
export DACAPO_PATH=$BINDING_PATH/dacapo
mkdir -p $DACAPO_PATH
wget https://downloads.sourceforge.net/project/dacapobench/archive/2006-10-MR2/dacapo-2006-10-MR2.jar -O $DACAPO_PATH/dacapo-2006-10-MR2.jar
