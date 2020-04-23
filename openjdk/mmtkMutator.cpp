
#include "mmtk.h"
#include "mmtkMutator.hpp"

MMTkMutatorContext MMTkMutatorContext::bind(::Thread* current) {
    return *((MMTkMutatorContext*) ::bind_mutator((void*) current));
}

HeapWord* MMTkMutatorContext::alloc(size_t bytes, Allocator allocator) {
    HeapWord* o = (HeapWord*) ::alloc((MMTk_Mutator) this, bytes, HeapWordSize, 0, allocator);
    // post_alloc((MMTk_Mutator) this, o, NULL, bytes, a);
    return o;
}
