
#include "precompiled.hpp"
#include "mmtk.h"
#include "mmtkMutator.hpp"

size_t MMTkMutatorContext::max_non_los_default_alloc_bytes = 0;

MMTkMutatorContext MMTkMutatorContext::bind(::Thread* current) {
  return *((MMTkMutatorContext*) ::bind_mutator((void*) current));
}

HeapWord* MMTkMutatorContext::alloc(size_t bytes, Allocator allocator) {
  // All allocations with size larger than max non-los bytes will get to this slowpath here.
  // We will use LOS for those.
  assert(MMTkMutatorContext::max_non_los_default_alloc_bytes != 0, "max_non_los_default_alloc_bytes hasn't been initialized");
  if (bytes >= MMTkMutatorContext::max_non_los_default_alloc_bytes) {
    allocator = AllocatorLos;
  }

  // FIXME: Proper use of slow-path api
  HeapWord* o = (HeapWord*) ::alloc((MMTk_Mutator) this, bytes, HeapWordSize, 0, allocator);
  // Post allococation. Currently we are only calling post_alloc in slowpath here.
  // TODO: We also need to call them in the fastpath.
  ::post_alloc((MMTk_Mutator) this, o, bytes, allocator);
  return o;
}

void MMTkMutatorContext::flush() {
  ::flush_mutator((MMTk_Mutator) this);
}
