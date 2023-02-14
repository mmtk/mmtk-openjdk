#!/bin/bash

# Compile with profiling support
RUSTFLAGS="-Cprofile-generate=/tmp/$USER/pgo-data" make CONF=linux-x86_64-normal-server-release THIRD_PARTY_HEAP=$PWD/../mmtk-openjdk/openjdk images

# Remove extraneous profiling data
rm -rf /tmp/$USER/pgo-data/*

# Profile using fop
MMTK_PLAN=GenImmix MMTK_STRESS_FACTOR=4194304 MMTK_PRECISE_STRESS=false ./build/linux-x86_64-normal-server-release/images/jdk/bin/java -XX:MetaspaceSize=500M -XX:+DisableExplicitGC -XX:-TieredCompilation -Xcomp -XX:+UseThirdPartyHeap -Xms60M -Xmx60M -jar /usr/share/benchmarks/dacapo/dacapo-evaluation-git-6e411f33.jar -n 5 fop

# Merge profiling data
/opt/rust/toolchains/1.66.1-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/bin/llvm-profdata merge -o /tmp/$USER/pgo-data/merged.profdata /tmp/$USER/pgo-data

# Compile using profiling data
RUSTFLAGS="-Cprofile-use=/tmp/$USER/pgo-data/merged.profdata -Cllvm-args=-pgo-warn-missing-function" make CONF=linux-x86_64-normal-server-release THIRD_PARTY_HEAP=$PWD/../mmtk-openjdk/openjdk images
