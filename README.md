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

The minimal supported Rust version for MMTk-OpenJDK binding is 1.57.0. Make sure your Rust version is higher than this. We test MMTk-OpenJDK
binding with Rust 1.59.0 (as specified in [`rust-toolchain`](mmtk/rust-toolchain)).
You may also need to use ssh-agent to authenticate with github (see [here](https://github.com/rust-lang/cargo/issues/3487) for more info):

```console
$ eval `ssh-agent`
$ ssh-add
```

### Getting Sources (for MMTk and VM)

To work on MMTk binding, we expect you have a directory structure like below. This section gives instructions on how to check out
those repositories with the correct version.

```
Your working directory/
├─ mmtk-openjdk/
│  ├─ openjdk/
│  └─ mmtk/
├─ openjdk/
└─ mmtk-core/ (optional)
```

#### Checkout Binding
First, clone this binding repo:

```console
$ git clone https://github.com/mmtk/mmtk-openjdk.git
```

The binding repo mainly consists of two folders, `mmtk` and `openjdk`.
* `mmtk` is logically a part of MMTk. It exposes APIs from `mmtk-core` and implements the `VMBinding` trait from `mmtk-core`.
* `openjdk` is logically a part of OpenJDK. When we build OpenJDK, we include this folder as if it is a part of the OpenJDK project.

#### Checkout OpenJDK

You would need our OpenJDK fork which includes the support for a third party heap (like MMTk). We assume you put `openjdk` as a sibling of `mmtk-openjdk`.
[`Cargo.toml`](mmtk/Cargo.toml) defines the version of OpenJDK that works with the version of `mmtk-openjdk`.

Assuming your current working directory is the parent folder of `mmtk-openjdk`, you can checkout out OpenJDK and the correct version using:
```console
$ git clone https://github.com/mmtk/openjdk.git
$ git -C openjdk checkout `sed -n 's/^openjdk_version.=."\(.*\)"$/\1/p' < mmtk-openjdk/mmtk/Cargo.toml`
```

#### Checkout MMTk core (optional)

The MMTk-OpenJDK binding points to a specific version of `mmtk-core` as defined in [`Cargo.toml`](mmtk/Cargo.toml). When you build the binding,
cargo will fetch the specified version of `mmtk-core`. If you would like to use
a different version or a local `mmtk-core` repo, you can checkout `mmtk-core` to a separate repo and modify the `mmtk` dependency in `Cargo.toml`.

For example, you can check out `mmtk-core` as a sibling of `mmtk-openjdk`.

```console
$ git clone https://github.com/mmtk/mmtk-core.git
```

And change the `mmtk` dependency in `Cargo.toml` (this assumes you put `mmtk-core` as a sibling of `mmtk-openjdk`):

```toml
mmtk = { path = "../../mmtk-core" }
```

## Build

_**Note:** MMTk is only tested with the `server` build variant._

After cloned the OpenJDK repo, cd into the root directiory:

```console
$ cd openjdk
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
$ make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$PWD/../mmtk-openjdk/openjdk
```

The output jdk is at `./build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk`.

### Location of Mark-bit
The location of the mark-bit can be specified by the environment variable
`MARK_IN_HEADER`. By default, the mark-bit is located on the side (in a side
metadata), but by setting the environment variable `MARK_IN_HEADER=1` while
building OpenJDK, we can change its location to be in the object's header:

```console
$ MARK_IN_HEADER=1 make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$PWD/../mmtk-openjdk/openjdk
```

### Alloc bit
To support the `global_alloc_bit` feature in mmtk-core, you can set the environment variable `GLOBAL_ALLOC_BIT=1` when
building OpenJDK. This will set the feature for mmtk-core, as well as compiling the fastpath for the alloc bit.

```console
$ GLOBAL_ALLOC_BIT=1 make CONF=linux-x86_64-normal-server-$DEBUG_LEVEL THIRD_PARTY_HEAP=$PWD/../mmtk-openjdk/openjdk
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
