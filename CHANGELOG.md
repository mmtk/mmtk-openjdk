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
