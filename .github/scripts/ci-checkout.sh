# ci-checkout.sh
set -ex

. $(dirname "$0")/common.sh

if [[ ! -z "$MMTK_REPO" && ! -z "$MMTK_VERSION" ]]; then
    # Replace repo and ref for mmtk-core. @ is used as delimeter for sed.
    sed -i "s@^mmtk.*git.*rev.*@mmtk = { git = \"https:\/\/github.com\/$MMTK_REPO.git\", rev = \"$MMTK_VERSION\" }@g" $BINDING_PATH/mmtk/Cargo.toml
fi

OPENJDK_URL=`sed -n 's/^openjdk_repo.=."\(.*\)"$/\1/p' $BINDING_PATH/mmtk/Cargo.toml`
OPENJDK_VERSION=`sed -n 's/^openjdk_version.=."\(.*\)"$/\1/p' $BINDING_PATH/mmtk/Cargo.toml`

rm -rf $OPENJDK_PATH
git clone $OPENJDK_URL $OPENJDK_PATH
git -C $OPENJDK_PATH checkout $OPENJDK_VERSION