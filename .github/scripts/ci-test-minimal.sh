set -ex

. $(dirname "$0")/common.sh
cur=$BINDING_PATH/.github/scripts

# This script is only used by MMTk core.
# OPENJDK_PATH is the default path set in ci-checkout.sh
export OPENJDK_PATH=$BINDING_PATH/repos/openjdk
export DEBUG_LEVEL=fastdebug
export TEST_JAVA_BIN=$OPENJDK_PATH/build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java

# Download dacapo
export DACAPO_PATH=$BINDING_PATH/dacapo
mkdir -p $DACAPO_PATH
wget https://downloads.sourceforge.net/project/dacapobench/archive/2006-10-MR2/dacapo-2006-10-MR2.jar -O $DACAPO_PATH/dacapo-2006-10-MR2.jar

# Normal build
$cur/ci-build.sh
# Test
MMTK_PLAN=SemiSpace runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=Immix runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=StickyImmix runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=GenCopy runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=MarkCompact runbms_dacapo2006_with_heap_multiplier fop 4
MMTK_PLAN=MarkSweep runbms_dacapo2006_with_heap_multiplier fop 8
MMTK_PLAN=NoGC runbms_dacapo2006_with_heap_size fop 1000 1000
# Test heap resizing
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_size fop 20 100
# Test no compressed oop
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_multiplier fop 4 -XX:-UseCompressedOops -XX:-UseCompressedClassPointers

# Build with vo bit
MMTK_VO_BIT=1 $cur/ci-build.sh
# Test
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_multiplier fop 4
