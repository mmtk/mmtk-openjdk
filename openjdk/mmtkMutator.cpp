
#include "mmtk.h"
#include "mmtkMutator.hpp"

MMTkMutatorContext MMTkMutatorContext::bind(::Thread* current) {
    return *((MMTkMutatorContext*) ::bind_mutator((void*) current));
}

HeapWord* MMTkMutatorContext::alloc(size_t bytes, Allocator allocator) {
    // FIXME: Proper use of slow-path api
    HeapWord* o = (HeapWord*) ::alloc((MMTk_Mutator) this, bytes, HeapWordSize, 0, allocator);
    // FIXME: Proper post alloc

    return o;
}

void MMTkMutatorContext::flush() {
    ::flush_mutator((MMTk_Mutator) this);
}