#include "precompiled.hpp"
#include "gc/shared/thirdPartyHeap.hpp"
#include "mmtk.h"
#include "thirdPartyHeapArguments.hpp"
#include "thirdPartyHeap.hpp"
#include "stdio.h"

namespace third_party_heap {

GCArguments* new_gc_arguments() {
  return NULL;
}

void register_finalizer(void* obj) {
  add_finalizer(obj);
}

};

// #endif
