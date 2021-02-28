
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
const int MAX_IMMIX_ALLOCATORS = 1;

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

struct ImmixAllocator {
    void* tls;
    void* cursor;
    void* limit;
    RustDynPtr space;
    RustDynPtr plan;
    uint8_t hot;
    uint8_t copy;
    void* large_cursor;
    void* large_limit;
    uint8_t request_for_large;
    uint8_t straddle;
    uintptr_t line_use_count;
    void* mark_table;
    uint8_t recyclable_block_tag;
    void* recyclable_block;
    uintptr_t line;
    uint8_t recyclable_exhausted;
};

struct Allocators {
    BumpAllocator bump_pointer[MAX_BUMP_ALLOCATORS];
    LargeObjectAllocator large_object[MAX_LARGE_OBJECT_ALLOCATORS];
    ImmixAllocator immix[MAX_IMMIX_ALLOCATORS];
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
};

#endif