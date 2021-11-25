# MMTk-OpenJDK

This repository provides binding between MMTk and OpenJDK.

## Contents

* [Requirements](#requirements)
* [Build](#build)
* [Test](#test)

## Requirements

We maintain an up to date list of the prerequisite for building MMTk and its bindings in the [mmtk-dev-env](https://github.com/mmtk/mmtk-dev-env) repository.
Please make sure your dev machine satisfies those prerequisites.

### Before you continue

If you use the set-up explained in [mmtk-dev-env](https://github.com/mmtk/mmtk-dev-env), make sure to set the default Rust toolchain to the one specified in [mmtk-dev-env](https://github.com/mmtk/mmtk-dev-env), e.g. by running:

```console
# replace nightly-YYYY-MM-DD with the the toolchain version specified in mmtk-dev-env
$ export RUSTUP_TOOLCHAIN=nightly-YYYY-MM-DD
```

You may also need to use ssh-agent to authenticate with github (see [here](https://github.com/rust-lang/cargo/issues/3487) for more info):

```console
$ eval `ssh-agent`
$ ssh-add
```

### Getting Sources (for MMTk and VM)

First, clone this binding repo:

```console
$ git clone --recursive --remote-submodules git@github.com:mmtk/mmtk-openjdk.git
```

The `mmtk-openjdk` binding repo is located under the `mmtk` folder, as a git-submodule of the OpenJDK repo.

The `mmtk-core` crate is a cargo dependency of the `mmtk-openjdk` binding repo.

The `openjdk` repo is at `./repos/openjdk`. And `./openjdk` contains mmtk's ThirdPartyHeap implementation files.

## Build

_**Note:** MMTk is only tested with the `server` build variant._

After cloned the OpenJDK repo, cd into the root directiory:

```console
$ cd mmtk-openjdk/repos/openjdk
```

Then select a `DEBUG_LEVEL`, can be one of `release`, `fastdebug`, `slowdebug` and `optimized`.

```console
$ # As an example, here we choose to build the release version
$ DEBUG_LEVEL=release
```

The differences between the four debug levels are:

| `$DEBUG_LEVEL` | Debug Info | Optimizations | Assertions | MMTk _Cargo-Build_ Profile |
| -------------- | ----------:| -------------:| ----------:| ------------------------:|
| `release`      |         ✘ |             ✔ |         ✘ |                  release |
| `optimized`    |         ✘ |             ✔ |         ✘ |                    debug |
| `fastdebug`    |         ✔ |             ✔ |         ✔ |                    debug |
| `slowdebug`    |         ✔ |             ✘ |         ✔ |                    debug |

If you are building for the first time, run the configure script:

```console
$ sh configure --disable-warnings-as-errors --with-debug-level=$DEBUG_LEVEL
```

Then build OpenJDK (this will build MMTk as well):

```console
$ make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$PWD/../../openjdk
```

The output jdk is at `./build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk`.

### Location of Mark-bit
The location of the mark-bit can be specified by the environment variable
`MARK_IN_HEADER`. By default, the mark-bit is located on the side (in a side
metadata), but by setting the environment variable `MARK_IN_HEADER=1` while
building OpenJDK, we can change its location to be in the object's header:

```console
$ MARK_IN_HEADER=1 make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$PWD/../../openjdk
```

### Alloc bit
To support the `global_alloc_bit` feature in mmtk-core, you can set the environment variable `GLOBAL_ALLOC_BIT=1` when
building OpenJDK. This will set the feature for mmtk-core, as well as compiling the fastpath for the alloc bit.

```console
$ GLOBAL_ALLOC_BIT=1 make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$PWD/../../openjdk
```

## Test

### Run HelloWorld (without MMTk)

```console
$ cat ./HelloWorld.java
class HelloWorld {
    public static void main(String[] args) {
        System.out.println("Hello World!");
    }
}
$ ./build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/javac HelloWorld.java
$ ./build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java HelloWorld
Hello World!
```

### Run DaCapo Benchmarks with MMTk

First, fetch DaCapo:
```console
$ wget https://sourceforge.net/projects/dacapobench/files/9.12-bach-MR1/dacapo-9.12-MR1-bach.jar/download -O ./dacapo-9.12-MR1-bach.jar
```

Run a DaCapo benchmark (e.g. `lusearch`):

```console
$ ./build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -Xms512M -Xmx512M -jar ./dacapo-9.12-MR1-bach.jar lusearch
Using scaled threading model. 24 processors detected, 24 threads used to drive the workload, in a possible range of [1,64]
===== DaCapo 9.12-MR1 lusearch starting =====
4 query batches completed
8 query batches completed
12 query batches completed
16 query batches completed
20 query batches completed
24 query batches completed
28 query batches completed
32 query batches completed
36 query batches completed
40 query batches completed
44 query batches completed
48 query batches completed
52 query batches completed
56 query batches completed
60 query batches completed
64 query batches completed
===== DaCapo 9.12-MR1 lusearch PASSED in 822 msec =====
```

**Note:** Pass `-XX:+UseThirdPartyHeap` as java command line arguments to enable MMTk.
