#!/bin/bash

set -xe

. $(dirname "$0")/new-common.sh

# Use fastdebug if DEBUG_LEVEL is unset
DEBUG_LEVEL=${DEBUG_LEVEL:="fastdebug"}

# Build product bundle
cd $OPENJDK_PATH
sh configure --disable-warnings-as-errors --with-debug-level=$DEBUG_LEVEL
make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$BINDING_PATH/openjdk product-bundles

if [[ $DEBUG_LEVEL == "fastdebug" ]]; then
    pushd build/linux-x86_64-normal-server-fastdebug/bundles
    F=`ls *_bin-debug.tar.gz`
    mv $F ${F/_bin-debug/_bin}
    popd
fi
