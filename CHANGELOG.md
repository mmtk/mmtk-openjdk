0.27.0 (2024-08-09)
===

## What's Changed
* Update to MMTK core PR #1159 by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/283

**Full Changelog**: https://github.com/mmtk/mmtk-openjdk/compare/v0.26.0...v0.27.0

0.26.0 (2024-07-01)
===

## What's Changed
* Rename edge to slot by @wks in https://github.com/mmtk/mmtk-openjdk/pull/274
* Fix deadlock related to safepoint sync. by @wks in https://github.com/mmtk/mmtk-openjdk/pull/279
* Add option to put forwarding bits on the side by @wks in https://github.com/mmtk/mmtk-openjdk/pull/277

**Full Changelog**: https://github.com/mmtk/mmtk-openjdk/compare/v0.25.0...v0.26.0

0.25.0 (2024-05-17)
===

## What's Changed
* Update mmtk-core to v0.25.0.
* Remove the coordinator thread by @wks in https://github.com/mmtk/mmtk-openjdk/pull/268
* Use to_address for SFT access by @wks in https://github.com/mmtk/mmtk-openjdk/pull/272
* Remove NULL ObjectReference by @wks in https://github.com/mmtk/mmtk-openjdk/pull/265
* Fix write barrier parameter type by @wks in https://github.com/mmtk/mmtk-openjdk/pull/273

**Full Changelog**: https://github.com/mmtk/mmtk-openjdk/compare/v0.24.0...v0.25.0

0.24.0 (2024-04-08)
===

## What's Changed
* Update mmtk-core to v0.24.0.
* Update Rust toolchain to 1.77.0.

**Full Changelog**: https://github.com/mmtk/mmtk-openjdk/compare/v0.23.0...v0.24.0

0.23.0 (2024-02-09)
===

## What's Changed
* Refactor CI test scripts by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/263
* Expose test scripts for minimal/extended tests for mmtk-core by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/266
* Add tests for sanity GC in ci-test-extended by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/267

**Full Changelog**: https://github.com/mmtk/mmtk-openjdk/compare/v0.22.0...v0.23.0

0.22.0 (2023-12-21)
===

## What's Changed
* Change README to make images target the default build command by @angussidney in https://github.com/mmtk/mmtk-openjdk/pull/253
* Add Java-specific constants from MMTk by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/258
* Post-release dependency version bump for v0.21.0 by @wks in https://github.com/mmtk/mmtk-openjdk/pull/259

## New Contributors
* @angussidney made their first contribution in https://github.com/mmtk/mmtk-openjdk/pull/253

**Full Changelog**: https://github.com/mmtk/mmtk-openjdk/compare/v0.21.0...v0.22.0

0.21.0 (2023-11-03)
===

## What's Changed
* Update to mmtk-core PR #988 by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/255
* Update to MMTk core PR #949 by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/240

**Full Changelog**: https://github.com/mmtk/mmtk-openjdk/compare/v0.20.0...v0.21.0

0.20.0 (2023-09-29)
===

## What's Changed
* Updating code to reflect API change by @udesou in https://github.com/mmtk/mmtk-openjdk/pull/238
* Fix Cargo.lock by @wenyuzhao in https://github.com/mmtk/mmtk-openjdk/pull/239
* Update tests to use dacapo-23.9-RC3-chopin by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/241
* Check results for new CI, allow some benchmarks to fail by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/211
* Fix ignored env var options by @wks in https://github.com/mmtk/mmtk-openjdk/pull/244
* Update pgo-build script to use the pinned Rust toolchain by @caizixian in https://github.com/mmtk/mmtk-openjdk/pull/236
* Compressed Oops Support by @wenyuzhao in https://github.com/mmtk/mmtk-openjdk/pull/235

## New Contributors
* @udesou made their first contribution in https://github.com/mmtk/mmtk-openjdk/pull/238

**Full Changelog**: https://github.com/mmtk/mmtk-openjdk/compare/v0.19.0...v0.20.0

0.19.0 (2023-08-18)
===

## What's Changed
* Rename alloc bit to valid object bit (VO bit) by @wks in https://github.com/mmtk/mmtk-openjdk/pull/214
* Fix invalid register values in `arraycopy_epilogue` barrier by @wenyuzhao in https://github.com/mmtk/mmtk-openjdk/pull/216
* Remove deprecated const. by @wks in https://github.com/mmtk/mmtk-openjdk/pull/217
* Update MMTk core PR #817 (alternative approach) by @wks in https://github.com/mmtk/mmtk-openjdk/pull/220
* Update to mmtk-core PR #838 by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/221
* Rename ambiguous `scan_thread_root{,s}` functions by @k-sareen in https://github.com/mmtk/mmtk-openjdk/pull/222
* Update to MMTk core PR #875 by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/225
* Show the version after cd by @wks in https://github.com/mmtk/mmtk-openjdk/pull/227
* Add features for genimmix and stickyimmix by @qinsoon in https://github.com/mmtk/mmtk-openjdk/pull/228
* Pass hotspot command line flags to mmtk-core by @wenyuzhao in https://github.com/mmtk/mmtk-openjdk/pull/229
* Fix unaligned edge access by @wks in https://github.com/mmtk/mmtk-openjdk/pull/232

**Full Changelog**: https://github.com/mmtk/mmtk-openjdk/compare/v0.18.0...v0.19.0


0.18.0 (2023-04-03)
===

* Support the `StickyImmix` plan.
* Fix a bug in the C2 compiler where we may update an object without any write barrier if C2 performs deoptimization and
  triggers a GC in deoptimization. We use `object_probable_write` to properly log the object.
* Update to OpenJDK 11.0.19+1 (`jdk-11.0.19+1-mmtk`).
* Update to mmtk-core 0.18.0.

0.17.0 (2023-02-17)
===

* MMTk OpenJDK binding now uses Rust 1.66.1 and MSRV is 1.61.0.
* Support dynamic heap resizing (enabled when `Xmx` and `Xms` values are different).
* Remove all inline directives. We rely on Rust compiler and PGO for inline decisions. Add provide a PGO guide.
* Fix a crash caused by null pointer access if the VM calls `CollectedHeap::soft_ref_policy()`.
* Update to mmtk-core 0.17.0.

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
