#!/bin/bash

# PGO seems to have problems with incremental compilation or something else similar.
# PGO build might fail with error messages such as
#   error: file `/tmp/$USER/pgo-data/merged.profdata` passed to `-C profile-use` does not exist.
# when the file clearly exists on disk.
# This happens on both 1.71.1 and 1.66.1, and running cargo clean prior to building seems to reliably work around the problem.
# We can remove this once the compiler bug is fixed.
pushd ../mmtk-openjdk/mmtk
cargo clean
popd

# Compile with profiling support
RUSTFLAGS="-Cprofile-generate=/tmp/$USER/pgo-data" make CONF=linux-x86_64-normal-server-release THIRD_PARTY_HEAP=$PWD/../mmtk-openjdk/openjdk images

# Remove extraneous profiling data
rm -rf /tmp/$USER/pgo-data/*

# Profile using fop
MMTK_PLAN=GenImmix MMTK_STRESS_FACTOR=16777216 MMTK_PRECISE_STRESS=false ./build/linux-x86_64-normal-server-release/images/jdk/bin/java -XX:MetaspaceSize=500M -XX:+DisableExplicitGC -XX:-TieredCompilation -Xcomp -XX:+UseThirdPartyHeap -Xms60M -Xmx60M -jar /usr/share/benchmarks/dacapo/dacapo-23.9-RC3-chopin.jar -n 5 fop

# Merge profiling data
/opt/rust/toolchains/1.71.1-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/bin/llvm-profdata merge -o /tmp/$USER/pgo-data/merged.profdata /tmp/$USER/pgo-data

pushd ../mmtk-openjdk/mmtk
cargo clean
popd

# Compile using profiling data
RUSTFLAGS="-Cprofile-use=/tmp/$USER/pgo-data/merged.profdata -Cllvm-args=-pgo-warn-missing-function" make CONF=linux-x86_64-normal-server-release THIRD_PARTY_HEAP=$PWD/../mmtk-openjdk/openjdk images
