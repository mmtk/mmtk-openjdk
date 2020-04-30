set -xe

# To OpenJDK folder
cd mmtk-openjdk/repos/openjdk

# Set DEBUG_LEVEL
export DEBUG_LEVEL=fastdebug

# config and build SemiSpace
export MMTK_PLAN=semispace
sh configure --disable-warnings-as-errors --with-debug-level=$DEBUG_LEVEL

# Test
./build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -jar benchmarks/dacapo-2006-10-MR2.jar fop