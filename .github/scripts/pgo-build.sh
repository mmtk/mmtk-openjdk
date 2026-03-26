#!/bin/bash
set -e

# This script should be run from the OpenJDK source directory. This is a quick check.
if [ "$(basename "$PWD")" != "openjdk" ] || [ ! -f "configure" ] || [ ! -d "src" ] || [ ! -d "test" ]; then
  echo "Error: Please run this script from the OpenJDK source directory."
  exit 1
fi

# The directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The directory of the binding
BINDING_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BINDING_MMTK_DIR="$BINDING_DIR/mmtk"
BINDING_OPENJDK_DIR="$BINDING_DIR/openjdk"
# Rust version
RUST_VERSION=$(tr -d '[:space:]' < "$BINDING_MMTK_DIR/rust-toolchain")

# llvm-profdata: we need to use the same version as the Rust compiler to avoid compatibility issues.
LLVM_PROFDATA="$(dirname "$(rustc +"$RUST_VERSION" --print target-libdir)")/bin/llvm-profdata"
# The dacapo jar used to profile.
DACAPO_JAR=/usr/share/benchmarks/dacapo/dacapo-23.11-MR2-chopin.jar

# OpenJDK config name
OPENJDK_DEBUG_LEVEL=release
OPENJDK_CONFIG=linux-x86_64-server-$OPENJDK_DEBUG_LEVEL

# Rust profile data directory
PROFILE_DATA_DIR="/tmp/$USER/pgo-data"

# PGO seems to have problems with incremental compilation or something else similar.
# PGO build might fail with error messages such as
#   error: file `/tmp/$USER/pgo-data/merged.profdata` passed to `-C profile-use` does not exist.
# when the file clearly exists on disk.
# This happens on both 1.71.1 and 1.66.1, and running cargo clean prior to building seems to reliably work around the problem.
# We can remove this once the compiler bug is fixed.
clean_binding_mmtk() {
  pushd "$BINDING_MMTK_DIR"
  cargo clean
  popd
}

clean_binding_mmtk

# Compile with profiling support
<<<<<<< HEAD
RUSTFLAGS="-Cprofile-generate=/tmp/$USER/pgo-data" make CONF=linux-x86_64-normal-server-release THIRD_PARTY_HEAP=$PWD/../mmtk-openjdk/openjdk images
=======
sh configure --disable-warnings-as-errors --with-debug-level=$OPENJDK_DEBUG_LEVEL
RUSTFLAGS="-Cprofile-generate=$PROFILE_DATA_DIR" make CONF=$OPENJDK_CONFIG THIRD_PARTY_HEAP=$BINDING_OPENJDK_DIR images
>>>>>>> 4037ffa (Improve pgo-build.sh (#351))

# Remove extraneous profiling data
rm -rf $PROFILE_DATA_DIR/*

# Profile using fop
<<<<<<< HEAD
MMTK_PLAN=GenImmix MMTK_STRESS_FACTOR=16777216 MMTK_PRECISE_STRESS=false ./build/linux-x86_64-normal-server-release/images/jdk/bin/java -XX:MetaspaceSize=500M -XX:+DisableExplicitGC -XX:-TieredCompilation -Xcomp -XX:+UseThirdPartyHeap -Xms60M -Xmx60M -jar /usr/share/benchmarks/dacapo/dacapo-23.9-RC3-chopin.jar -n 5 fop

# Merge profiling data
/opt/rust/toolchains/1.71.1-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/bin/llvm-profdata merge -o /tmp/$USER/pgo-data/merged.profdata /tmp/$USER/pgo-data
=======
MMTK_PLAN=GenImmix MMTK_STRESS_FACTOR=16777216 MMTK_PRECISE_STRESS=false ./build/$OPENJDK_CONFIG/images/jdk/bin/java -XX:MetaspaceSize=500M -XX:+DisableExplicitGC -XX:-TieredCompilation -Xcomp -XX:+UseThirdPartyHeap -Xms60M -Xmx60M -jar $DACAPO_JAR -n 5 fop

# Merge profiling data
"$LLVM_PROFDATA" merge -o $PROFILE_DATA_DIR/merged.profdata $PROFILE_DATA_DIR
>>>>>>> 4037ffa (Improve pgo-build.sh (#351))

clean_binding_mmtk

# Compile using profiling data
<<<<<<< HEAD
RUSTFLAGS="-Cprofile-use=/tmp/$USER/pgo-data/merged.profdata -Cllvm-args=-pgo-warn-missing-function" make CONF=linux-x86_64-normal-server-release THIRD_PARTY_HEAP=$PWD/../mmtk-openjdk/openjdk images
=======
RUSTFLAGS="-Cprofile-use=$PROFILE_DATA_DIR/merged.profdata -Cllvm-args=-pgo-warn-missing-function" make CONF=$OPENJDK_CONFIG THIRD_PARTY_HEAP=$BINDING_OPENJDK_DIR images
>>>>>>> 4037ffa (Improve pgo-build.sh (#351))
