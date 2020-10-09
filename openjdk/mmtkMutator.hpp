
#ifndef SHARE_VM_GC_MMTK_MMTKMUTATOR_HPP
#define SHARE_VM_GC_MMTK_MMTKMUTATOR_HPP

#include "mmtk.h"
#include "utilities/globalDefinitions.hpp"

enum Allocator {
    AllocatorDefault = 0,
    AllocatorNonReference = 1,
    AllocatorNonMoving = 2,
    AllocatorImmortal = 3,
    AllocatorLos = 4,
    AllocatorPrimitiveLos = 5,
    AllocatorGcSpy = 6,
    AllocatorCode = 7,
    AllocatorLargeCode = 8,
    AllocatorAllocators = 9,
    AllocatorDefaultSite = -1,
};

struct MMTkMutatorContext {
    // ss
    void* ss_tls;
    void* ss_cursor;
    void* ss_limit;
    // The `space` field in mmtk's mutator struct is a fat pointer,
    // which takes up 2 words.
    void* ss_space;
    void* ss_space_fat;
    void* ss_plan;
    // vs
    void* vs_tls;
    void* vs_cursor;
    void* vs_limit;
    void* vs_space;
    void* vs_space_fat;
    void* vs_plan;
    // los
    void* los_tls;
    void* los_space;
    void* los_space_fat;
    void* los_plan;
    //,
    void* plan;
    void* modbuf;

    HeapWord* alloc(size_t bytes, Allocator allocator = AllocatorDefault);

    void deinit();

    static MMTkMutatorContext bind(::Thread* current);
};

#endif