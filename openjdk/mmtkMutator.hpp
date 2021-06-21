
#ifndef SHARE_VM_GC_MMTK_MMTKMUTATOR_HPP
#define SHARE_VM_GC_MMTK_MMTKMUTATOR_HPP

#include "mmtk.h"
#include "utilities/globalDefinitions.hpp"

enum Allocator {
    AllocatorDefault = 0,
    AllocatorImmortal = 1,
    AllocatorLos = 2,
    AllocatorCode = 3,
    AllocatorReadOnly = 4,
};

struct RustDynPtr {
    void* data;
    void* vtable;
};

// These constants should match the constants defind in mmtk::util::alloc::allocators
const int MAX_BUMP_ALLOCATORS = 5;
const int MAX_LARGE_OBJECT_ALLOCATORS = 1;
const int MAX_MALLOC_ALLOCATORS = 1;
const int MAX_FREE_LIST_ALLOCATORS = 1;

// The following types should have the same layout as the types with the same name in MMTk core (Rust)

struct BumpAllocator {
    void* tls;
    void* cursor;
    void* limit;
    RustDynPtr space;
    RustDynPtr plan;
};

struct LargeObjectAllocator {
    void* tls;
    void* space;
    RustDynPtr plan;
};

struct MallocAllocator {
    void* tls;
    void* space;
    RustDynPtr plan;
};

struct FreeListAllocator {
    void* tls;
    void* space;
    RustDynPtr plan;
    void* available_blocks;
    void* exhausted_blocks;
    void* free_lists;
};

struct Allocators {
    BumpAllocator bump_pointer[MAX_BUMP_ALLOCATORS];
    LargeObjectAllocator large_object[MAX_LARGE_OBJECT_ALLOCATORS];
    MallocAllocator malloc[MAX_MALLOC_ALLOCATORS];
    FreeListAllocator free_list[MAX_FREE_LIST_ALLOCATORS];
};

struct MutatorConfig {
    void* allocator_mapping;
    void* space_mapping;
    RustDynPtr prepare_func;
    RustDynPtr release_func;
};

struct MMTkMutatorContext {
    Allocators allocators;
    RustDynPtr barrier;
    void* mutator_tls;
    RustDynPtr plan;
    MutatorConfig config;

    HeapWord* alloc(size_t bytes, Allocator allocator = AllocatorDefault);

    void flush();

    static MMTkMutatorContext bind(::Thread* current);

    // Max object size that does not need to go into LOS. We get the value from mmtk-core, and cache its value here.
    static size_t max_non_los_default_alloc_bytes;
};

#endif