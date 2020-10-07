
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

struct BumpAllocator {
    void* tls;
    void* cursor;
    void* limit;
    RustDynPtr space;
    void* plan;
};

struct LargeObjectAllocator {
    void* tls;
    void* space;
    void* plan;
};

const int MAX_BUMP_ALLOCATORS = 5;
const int MAX_LARGE_OBJECT_ALLOCATORS = 1;

struct Allocators {
    BumpAllocator bump_pointer[MAX_BUMP_ALLOCATORS];
    LargeObjectAllocator large_object[MAX_LARGE_OBJECT_ALLOCATORS];
};

struct MMTkMutatorContext {
    Allocators allocators;
    void* mutator_tls;
    void* plan;
    void* config;

    HeapWord* alloc(size_t bytes, Allocator allocator = AllocatorDefault);

    static MMTkMutatorContext bind(::Thread* current);
};

#endif