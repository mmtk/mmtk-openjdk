0.16.0 (2022-12-06)
===

* MMTk OpenJDK binding now uses Rust edition 2021.
* Support MMTk's native mark sweep plan.
* Rename a few functions so they have consistent names across Rust and C++.
* Fix a compilation error when barrier fast path is disabled.
* Fix the wrong function pointer type in Rust that refers to a native function pointer.
* Update to mmtk-core 0.16.0.

0.15.0 (2022-09-20)
===

* Add MMTk build info to `-Xinternalversion`.
* Implement `arraycopy` barriers.
* Update to mmtk-core 0.15.0.

0.14.1 (2022-08-10)
===

* Fix a bug that MMTk gets initialized even when we are not using MMTk's GC.

0.14.0 (2022-08-08)
===

* Remove incorrect `MMTkRootScanWorkScope`.
* Remove unused `compute_*_roots` functions.
* Optimize `CodeCache` roots scanning.
* Fix a bug that `mmtk_start_the_world_count` may be incorrect.
* Update documentation about evaluation builds.
* Inlucde `Cargo.lock` in the repository.
* Update to mmtk-core 0.14.0.

0.13.0 (2022-06-27)
===

* Fixes a bug that may cause programs to hang in stop-the-world synchronization.
* Updates to mmtk-core 0.13.0.

0.12.0 (2022-05-13)
===

* Adds a few missing includes.
* Adds weak reference support (It is disabled by default. Set MMTk option `no_reference_types` to `false` to enable it).
* Fixes a bug in C2 allocation fastpath generation for mark compact which caused significant slowdown for mark compact allocation.
* Fixes a bug in transitioning thread state for the `harness_begin` call which may cause a 'deadlock in safepoint code' error.
* Updates the OpenJDK version to 11.0.15+8.
* Updates to mmtk-core 0.12.0.

0.11.0 (2022-04-01)
===

* The OpenJDK submodule is removed from the repo. We now record the VM version
  in `[package.metadata.openjdk]` in the Cargo manifest `Cargo.toml`.
* The OpenJDK binding now builds with stable Rust toolchains.
* Removes `object_alignment` from `OpenJDK_Upcalls`.
* Implements `ObjectModel::get_reference_when_copied_to()`.
* Updates to mmtk-core 0.11.0.

0.10.0 (2022-02-14)
===

* Implements a fastpath for `ObjectModel::get_current_size()` in Rust.
* Supports setting MMTk options by `-XX:THIRD_PARTY_HEAP_OPTIONS=`
* Supports proper OutOfMemory exceptions.
* Updates to mmtk-core 0.10.0.

0.9.0 (2021-12-16)
===

* Supports the `MarkCompact` plan.
* Updates to mmtk-core 0.9.0.

0.8.0 (2021-11-01)
===

* Introduces A VM companion thread to trigger safe point synchronisation. This fixed a bug that
  MMTk's call to `SafePointerSynchronize::begin()` may race with the OpenJDK's VM thread.
* Changes `COORDINATOR_ONLY_STW` to `false`. Stopping and resuming mutators are done by the companion thread,
  amd it is no longer a requirement for them to be done by the same GC thread.
* Fixes a bug that for some allocations, both fastpath and slowpath were invoked.
* Fixes a bug in generating code to set the alloc bit in C1 compiler.
* Fixes a bug that some derived pointers were missing as roots.
* Updates to mmtk-core 0.8.0.

0.7.0 (2021-09-22)
===

* Supports the `GenImmix` plan.
* Supports the `global_alloc_bit` feature in mmtk-core.
* Fixes monitor misuse in the finalizer thread.
* Fixes style for C++ code to match OpenJDK style guidelines.
* Updates to mmtk-core 0.7.0.

0.6.0 (2021-08-10)
===

* Supports the `Immix` plan.
* Uses side mark bit by default. Adds a feature 'mark_bit_in_header' to switch to in-header mark bit.
* Adds a size check for allocation so over-sized objects will be allocated to large object space.
* Updates to mmtk-core 0.6.0.

0.5.0 (2021-06-28)
===

* Supports the new `PageProtect` plan, added to help debugging.
* Updates `ObjectModel` to support the new metadata structure, where the bindings decide whether to put each per-object metadata on side or in object header.
* Updates to mmtk-core 0.5.0.

0.4.0 (2021-05-17)
===

* Fixes a bug where benchmarks failed randomly due to duplicate edges
* Switches to our new OpenJDK fork (`11.0.11+6-mmtk`) which is based-on [OpenJDK-11 update repo](https://github.com/openjdk/jdk11u.git)
* Adds style checks
* Cleans up some unused code
* Updates to mmtk-core 0.4.0


0.3.0 (2021-04-01)
===

* Supports the `marksweep` plan in mmtk-core.
* Supports fastpath for object barrier (used in `gencopy`).
* Supports finalization
* Supports runtime plan selection (through the environment variable `MMTK_PLAN`)
* Updates to mmtk-core 0.3.0


0.2.0 (2020-12-18)
===

* Supports the `gencopy` plan in mmtk-core.
* Fixes a bug for incorrect heap boundary check.
* Updated to mmtk-core 0.2.0.


0.1.0 (2020-11-04)
===

* Supports the following plans from mmtk-core:
  * NoGC
  * SemiSpace
