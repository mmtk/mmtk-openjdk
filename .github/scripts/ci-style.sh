set -xe

. $(dirname "$0")/common.sh

export RUSTFLAGS="-D warnings"

pushd $BINDING_PATH/mmtk
cargo clippy
cargo clippy --release

cargo fmt -- --check
popd

# Allow all files to be checked, and exit if any went wrong.
set +xe

ANY_LINE_END_ERRORS=0
for FN in $(find $BINDING_PATH/openjdk \
    $BINDING_PATH/mmtk \
    '(' \
    -name '*.hpp' \
    -o -name '*.cpp' \
    -o -name '*.rs' \
    -o -name '*.toml' \
    -o -name '*.gmk' \
    ')')
do
    $(dirname "$0")/ci-check-lineends.sh "$FN"
    if [[ $? -ne 0 ]]; then
        ANY_LINE_END_ERRORS=1
    fi
done

if [[ $ANY_LINE_END_ERRORS -ne 0 ]]; then
    exit 1
fi
