
#ifndef SHARE_VM_GC_MMTK_MMTKMUTATOR_HPP
#define SHARE_VM_GC_MMTK_MMTKMUTATOR_HPP

#include "mmtk.h"
#include "utilities/globalDefinitions.hpp"



struct MMTkMutatorContext {
    // ss
    void* ss_tls;
    void* ss_cursor;
    void* ss_limit;
    void* ss_space;
    void* ss_plan;
    // vs
    void* vs_tls;
    void* vs_cursor;
    void* vs_limit;
    void* vs_space;
    void* vs_plan;
    // los
    void* los_tls;
    void* los_space;
    void* los_plan;
    //,
    void* plan;

    inline HeapWord* alloc(size_t bytes) {
        HeapWord* o = (HeapWord*) ::alloc((MMTk_Mutator) this, bytes, HeapWordSize, 0, 0);
        // printf("Alloc %p\n", o);
        return o;
        // bytes = (bytes + (HeapWordSize - 1)) & ~(HeapWordSize - 1);
        // if (_cursor + bytes <= _limit) {
        //     HeapWord* start = (HeapWord*) _cursor;
        //     _cursor = _cursor + bytes;
        //     return start;
        // } else {
        //     return (HeapWord*) alloc_slow((MMTk_Mutator) this, bytes, HeapWordSize, 0, 0);
        // }
    }

    static MMTkMutatorContext bind(::Thread* current);
};

#endif