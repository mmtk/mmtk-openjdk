# ci-checkout.sh
set -ex

. $(dirname "$0")/common.sh

OPENJDK_URL=`sed -n 's/^openjdk_repo.=."\(.*\)"$/\1/p' $BINDING_PATH/mmtk/Cargo.toml`
OPENJDK_VERSION=`sed -n 's/^openjdk_version.=."\(.*\)"$/\1/p' $BINDING_PATH/mmtk/Cargo.toml`

rm -rf $OPENJDK_PATH
git clone $OPENJDK_URL $OPENJDK_PATH
git -C $OPENJDK_PATH checkout $OPENJDK_VERSION
