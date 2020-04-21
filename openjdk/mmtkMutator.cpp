
#include "mmtk.h"
#include "mmtkMutator.hpp"

MMTkMutatorContext MMTkMutatorContext::bind(::Thread* current) {
    return *((MMTkMutatorContext*) ::bind_mutator((void*) current));
}

HeapWord* MMTkMutatorContext::alloc(size_t bytes) {
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
