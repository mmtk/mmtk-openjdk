# ci-checkout.sh
set -ex

. $(dirname "$0")/common.sh

OPENJDK_URL=`cargo read-manifest --manifest-path=$BINDING_PATH/mmtk/Cargo.toml | python3 -c 'import json,sys; print(json.load(sys.stdin)["metadata"]["openjdk"]["openjdk_repo"])'`
OPENJDK_VERSION=`cargo read-manifest --manifest-path=$BINDING_PATH/mmtk/Cargo.toml | python3 -c 'import json,sys; print(json.load(sys.stdin)["metadata"]["openjdk"]["openjdk_version"])'`

rm -rf $OPENJDK_PATH
git clone $OPENJDK_URL $OPENJDK_PATH
git -C $OPENJDK_PATH checkout $OPENJDK_VERSION
