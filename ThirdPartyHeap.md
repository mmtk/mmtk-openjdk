# OpenJDK's Third-Party Heap design



## Build System Integration

#### 0. Suppose our implementation of third-party heap is under folder `$PWD/my_third_party_heap/`

#### 1. Enable build with third-party heap

* Build with command `make THIRD_PARTY_HEAP=$PWD/my_third_party_heap`.
* This will search for, and include `$PWD/my_third_party_heap/CompileThirdPartyHeap.gmk`

#### 2. Setup `my_third_party_heap/CompileThirdPartyHeap.gmk`

```makefile
# TOPDIR points to openjdk root directory
JVM_SRC_DIRS += $(TOPDIR)/my_third_party_heap
JVM_CFLAGS += -DTHIRD_PARTY_HEAP -DTHIRD_PARTY_HEAP_SRC=$(TOPDIR)/my_third_party_heap
```
This will compile every `.cpp` files under `$PWD/my_third_party_heap` and link to _libjava.so_.

#### 3. (Optional) Add custom build target before building _libjava.so_

This is useful for mmtk to build the _libmmtk.so_ building _libjava.so_

Add this to `CompileThirdPartyHeap.gmk`:

```makefile
lib_mmtk:
	echo "your build commands here"

$(BUILD_LIBJVM): lib_mmtk
```

## Third-Party Heap Interface

Currently, we need to add a cpp file in `my_third_party_heap` and implement 2 interfaces:

```cpp
#include "gc/shared/thirdPartyHeap.hpp"

namespace third_party_heap {

class MutatorContext;

MutatorContext* bind_mutator(::Thread* current) {
    // Returns a pointer of a thread-local storage
}

GCArguments* new_gc_arguments() {
    // Return your inherited GCArguments instance
}

};
```

* An implementation of `GCArguments` is responsible for create a `CollectedHeap*`.
  1. So you need to define a implementation of `CollectedHeap` as well.
  2. And implement `CollectedHeap::mem_allocate` method for object allocation
     1. Allocate objects either by:
        * Allocate with global synchronization
        * Get pre-created `MutatorContext*`, and allocate without synchronization.
     2. (Possibly) triggers a gc manually within this method


## TODO: Barrier Support for Third-party Heap

We only have no-barrier support...

## TODO: Performance

`CollectedHeap::mem_allocate` without `UseTLAB` seems have bad performance...
