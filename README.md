# mmtk-openjdk
This repository provides binding between MMTk and OpenJDK. 

## Table of Content
* [Requirements](#requirements)
* [Build](#build)
* [Test](#test)

## Requirements

This sections describes prerequisite for building OpenJDK with MMTk.

### Before You Start

#### Software Dependencies

* git, make, jdk and the gcc toolchain
  * You can install them simply via `sudo apt install git build-essential default-jdk`.
* Rustup nightly toolchain
  * Please visit [rustup.rs](https://rustup.rs/) for installation instructions.

#### Supported Hardware

MMTk/OpenJDK only supports `linux-x86_64`.

_Tested on a Ryzen 9 3900X Machine with 32GB RAM, running Ubuntu 18.04-amd64 (Linux kernel version 4.15.0-21-generic)._

### Getting Sources (for MMTk and VM)

First, clone this binding repo:

```console
$ git clone --recursive git@gitlab.anu.edu.au:mmtk/mmtk-openjdk.git
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

### Run DaCapo Benchmarks with MMTk (on a moma machine)

**Note:** Pass `-XX:+UseThirdPartyHeap` as java command line arguments to enable MMTk.

```console
$ ./build/linux-x86_64-normal-server-$DEBUG_LEVEL/jdk/bin/java -XX:+UseThirdPartyHeap -Xms512M -Xmx512M -jar /usr/share/benchmarks/dacapo/dacapo-9.12-bach.jar lusearch
Using scaled threading model. 24 processors detected, 24 threads used to drive the workload, in a possible range of [1,64]
===== DaCapo 9.12 lusearch starting =====
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
===== DaCapo 9.12 lusearch PASSED in 1618 msec =====
```
