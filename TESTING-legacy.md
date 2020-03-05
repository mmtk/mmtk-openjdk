# Testing OpenJDK+MMTk on the Momas

> Last tested: OpenJDK@c76b64be MMTk@e02d62d6 2018-07-14 by Felix Friedlander

## Environment and dependencies

GCC 7.3 should already be installed, but it will not be the default. When
running `configure`, we will need to pass `CC` and `CXX` explicitly.

OpenJDK 8 is installed, but we want a newer boot JDK. Download OpenJDK 10 from
[java.net](http://jdk.java.net/10/) and extract it:

```console
$ curl -O https://download.java.net/java/GA/jdk10/10.0.2/19aef61b38124481863b1413dce1855f/13/openjdk-10.0.2_linux-x64_bin.tar.gz
$ tar -xf openjdk-10.0.2_linux-x64_bin.tar.gz
```

An appropriate version of Rust should already be installed. You can check this
using `rustup`:

```console
$ rustup show
```

## Building (debug)

`<DEBUG_LEVEL>` can be one of `release`, `fastdebug`, `slowdebug` and `optimized`.

```bash
cd openjdk
# Build MMTk
cd mmtk
cargo +nightly build
cd ..
# Build OpenJDK
bash configure --disable-warnings-as-errors --with-debug-level=<DEBUG_LEVEL>
CONF=linux-x86_64-normal-server-<DEBUG_LEVEL> make
# JDK is at `build/linux-x86_64-normal-server-<DEBUG_LEVEL>/jdk`
```

## Building (release)

```bash
cd openjdk
# Build MMTk
cd mmtk
cargo +nightly build --release
cd ..
# Build OpenJDK
bash configure --disable-warnings-as-errors
CONF=linux-x86_64-normal-server-release make
# JDK is at `build/linux-x86_64-normal-server-release/jdk`
```

## Testing

1. `java` binary is at `build/linux-x86_64-normal-server-<DEBUG_LEVEL>/jdk/bin/java`.
2. Set env `LD_LIBRARY_PATH` to include `$PWD/mmtk/vmbindings/openjdk/target/debug` (or `$PWD/mmtk/vmbindings/openjdk/target/release` if openjdk is built with debug level `release`).
3. To enable MMTk, pass `-XX:+UseMMTk -XX:-UseCompressedOops` to `java`.

e.g.:

* If `DEBUG_LEVEL` = `fastdebug`, `slowdebug` or `optimized`:
```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PWD/mmtk/vmbindings/openjdk/target/debug
build/linux-x86_64-normal-server-fastdebug/jdk/bin/java -XX:+UseMMTk -XX:-UseCompressedOops HelloWorld
```

* If `DEBUG_LEVEL` = `release`:
```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PWD/mmtk/vmbindings/openjdk/target/release
build/linux-x86_64-normal-server-release/jdk/bin/java -XX:+UseMMTk -XX:-UseCompressedOops HelloWorld
```

> Original instructions by Abdullah Al Mamun and Tanveer Hannan
>
> Updated Sep 2018 by Felix Friedlander
> 
> Updated Feb 2020 by Wenyu Zhao