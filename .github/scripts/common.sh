BINDING_PATH=$(realpath $(dirname "$0"))/../..
RUSTUP_TOOLCHAIN=`cat $BINDING_PATH/mmtk/rust-toolchain`

# dacapo2006 min heap for mark compact
MINHEAP_ANTLR=5
MINHEAP_FOP=13
MINHEAP_LUINDEX=6
MINHEAP_LUSEARCH=8
MINHEAP_PMD=24
MINHEAP_HSQLDB=117
MINHEAP_ECLIPSE=23
MINHEAP_XALAN=21

# ensure_env 'var_name'
ensure_env() {
    env_var=$1

    if ! [[ -v $env_var ]]; then
        echo "Environment Variable "$env_var" is required. "
        exit 1
    fi
}

runbms_dacapo2006_with_heap_multiplier()
{
    benchmark=$1
    heap_multiplier=$2

    minheap_env="MINHEAP_${benchmark^^}"
    minheap_value="${!minheap_env}"
    heap_size=$((minheap_value * heap_multiplier))

    shift 2

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

    ensure_env TEST_JAVA_BIN
    ensure_env DACAPO_PATH

    $TEST_JAVA_BIN -XX:+UseThirdPartyHeap -server -XX:MetaspaceSize=100M -Xms$min_heap_str -Xmx$max_heap_str $@ -jar $DACAPO_PATH/dacapo-2006-10-MR2.jar $benchmark
}
