
#include "precompiled.hpp"
#include "mmtk.h"
#include "mmtkMutator.hpp"
#include "mmtkHeap.hpp"

size_t MMTkMutatorContext::max_non_los_default_alloc_bytes = 0;

MMTkMutatorContext MMTkMutatorContext::bind(::Thread* current) {
  if (IMMIX_ALLOCATOR_SIZE != sizeof(ImmixAllocator)) {
    printf("ERROR: Unmatched immix allocator size: rs=%zu cpp=%zu\n", IMMIX_ALLOCATOR_SIZE, sizeof(ImmixAllocator));
    guarantee(false, "ERROR");
  }
  if (FREE_LIST_ALLOCATOR_SIZE != sizeof(MMTkFreeListAllocator)) {
    printf("ERROR: Unmatched free list allocator size: rs=%zu cpp=%zu\n", FREE_LIST_ALLOCATOR_SIZE, sizeof(MMTkFreeListAllocator));
    guarantee(false, "ERROR");
  }
  auto original_rust_mutator_pointer = (MMTkMutatorContext*) ::bind_mutator((void*) current);
  MMTkMutatorContext context = *original_rust_mutator_pointer;
  context.original_rust_mutator_pointer = original_rust_mutator_pointer;
  return context;
}

bool MMTkMutatorContext::is_ready_to_bind() {
  return ::openjdk_is_gc_initialized();
}

HeapWord* MMTkMutatorContext::alloc(size_t bytes, Allocator allocator) {
  // All allocations with size larger than max non-los bytes will get to this slowpath here.
  // We will use LOS for those.
  assert(MMTkMutatorContext::max_non_los_default_alloc_bytes != 0, "max_non_los_default_alloc_bytes hasn't been initialized");
  if (bytes >= MMTkMutatorContext::max_non_los_default_alloc_bytes) {
    allocator = AllocatorLos;
  } else {
    AllocatorSelector selector = MMTkHeap::heap()->default_allocator_selector;
    if (selector.tag == TAG_IMMIX && !disable_fast_alloc()) {
      auto& allocator = allocators.immix[selector.index];
      auto cursor = uintptr_t(allocator.cursor);
      auto limit = uintptr_t(allocator.limit);
      if (cursor + bytes <= limit) {
        allocator.cursor = (void*) (cursor + bytes);
        return (HeapWord*) cursor;
      }
    }
  }

  // FIXME: Proper use of slow-path api
  HeapWord* o = (HeapWord*) ::alloc((MMTk_Mutator) this, bytes, HeapWordSize, 0, allocator);
  // Post allocation hooks. Note that we can get a nullptr from mmtk core in the case of OOM.
  // Hence, only call post allocation hooks if we have a proper object.
  if (o != nullptr && allocator != AllocatorDefault) {
    ::post_alloc((MMTk_Mutator) this, o, bytes, allocator);
  }
  return o;
}

void MMTkMutatorContext::flush() {
  ::flush_mutator((MMTk_Mutator) this);
}

void MMTkMutatorContext::destroy() {
  ::destroy_mutator((MMTk_Mutator) this);
  // if (original_rust_mutator_pointer != NULL) {
  //   *original_rust_mutator_pointer = *this;
  //   release_mutator(original_rust_mutator_pointer);
  //   original_rust_mutator_pointer = NULL;
  // }
}
