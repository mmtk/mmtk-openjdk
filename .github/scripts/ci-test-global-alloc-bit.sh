set -xe

unset JAVA_TOOL_OPTIONS

# To OpenJDK folder
root_dir=$(dirname "$0")/../../
cd $root_dir/repos/openjdk

# Choose build: use slowdebug for shorter build time (32m user time for release vs. 20m user time for slowdebug)
export DEBUG_LEVEL=fastdebug
export MMTK_EXTREME_ASSERTIONS=0
export GLOBAL_ALLOC_BIT=1
echo $RUSTUP_TOOLCHAIN

# Build
sh configure --disable-warnings-as-errors --with-debug-level=$DEBUG_LEVEL
make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$PWD/../../openjdk

# --- SemiSpace ---
export MMTK_PLAN=SemiSpace

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar luindex


# --- Immix ---
export MMTK_PLAN=Immix

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar luindex

# --- GenImmix ---
export MMTK_PLAN=GenImmix

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar luindex


# --- GenCopy ---
export MMTK_PLAN=GenCopy

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar luindex

# --- NoGC ---

export MMTK_PLAN=NoGC

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms1G -Xmx1G -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms1G -Xmx1G -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms1G -Xmx1G -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar luindex


# --- MarkSweep ---
export MMTK_PLAN=MarkSweep

build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar antlr
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar fop
build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms500M -Xmx500M -XX:-TieredCompilation -Xcomp -jar benchmarks/dacapo-2006-10-MR2.jar luindex

