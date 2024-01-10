BINDING_PATH=$(realpath $(dirname "$0"))/../..
OPENJDK_PATH=$BINDING_PATH/repos/openjdk
DACAPO_PATH=$OPENJDK_PATH/dacapo

RUSTUP_TOOLCHAIN=`cat $BINDING_PATH/mmtk/rust-toolchain`

DEBUG_LEVEL=fastdebug

# dacapo2006 min heap for mark compact
MINHEAP_ANTLR=5
MINHEAP_FOP=13
MINHEAP_LUINDEX=6
MINHEAP_LUSEARCH=8
MINHEAP_PMD=24
MINHEAP_HSQLDB=117
MINHEAP_ECLIPSE=23
MINHEAP_XALAN=21

runbms_dacapo2006_with_heap_multiplier()
{
    benchmark=$1
    heap_multiplier=$2

    minheap_env="MINHEAP_${benchmark^^}"
    minheap_value="${!minheap_env}"
    heap_size=$((minheap_value * heap_multiplier))

    shift

    runbms_dacapo2006_with_heap_size $benchmark $heap_size $heap_size $@
}

runbms_dacapo2006_with_heap_size()
{
    benchmark=$1
    min_heap=$2
    max_heap=$3

    min_heap_str="${min_heap}M"
    max_heap_str="${max_heap}M"

    shift 3

    $JAVA_BIN -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms$min_heap_str -Xmx$max_heap_str $@ -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar $benchmark
}
