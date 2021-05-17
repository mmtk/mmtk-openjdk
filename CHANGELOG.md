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
