
#include "mmtk.h"
#include "mmtkMutator.hpp"

MMTkMutatorContext MMTkMutatorContext::bind(::Thread* current) {
    return *((MMTkMutatorContext*) ::bind_mutator((void*) current));
}

HeapWord* MMTkMutatorContext::alloc(size_t bytes, Allocator allocator) {
    // FIXME: Proper use of slow-path api
#if MMTK_GC_NOGC
    HeapWord* o = (HeapWord*) ::alloc((MMTk_Mutator) this, bytes, HeapWordSize, 0, allocator);
    // post_alloc((MMTk_Mutator) this, o, NULL, bytes, a);
    return o;
#elif MMTK_GC_SEMISPACE
    HeapWord* o = (HeapWord*) ::alloc((MMTk_Mutator) this, bytes, HeapWordSize, 0, allocator);
    // post_alloc((MMTk_Mutator) this, o, NULL, bytes, a);
    return o;
#elif MMTK_GC_GENCOPY
    HeapWord* o = (HeapWord*) ::alloc((MMTk_Mutator) this, bytes, HeapWordSize, 0, allocator);
    // post_alloc((MMTk_Mutator) this, o, NULL, bytes, a);
    return o;
#else
    #error "GC is not specified"
#endif
}
