
#include "mmtk.h"
#include "mmtkMutator.hpp"

MMTkMutatorContext MMTkMutatorContext::bind(::Thread* current) {
    return *((MMTkMutatorContext*) ::bind_mutator((void*) current));
}

HeapWord* MMTkMutatorContext::alloc(size_t bytes, Allocator allocator) {
#ifdef MMTK_GC_NOGC
    HeapWord* o = (HeapWord*) ::alloc_slow_bump_monotone_immortal((MMTk_Mutator) this, bytes, HeapWordSize, 0, allocator);
    // post_alloc((MMTk_Mutator) this, o, NULL, bytes, a);
    return o;
#elif MMTK_GC_SEMISPACE
    HeapWord* o = (HeapWord*) ::alloc_slow_bump_monotone_copy((MMTk_Mutator) this, bytes, HeapWordSize, 0, allocator);
    // post_alloc((MMTk_Mutator) this, o, NULL, bytes, a);
    return o;
#else
    #error "GC is not specified"
#endif
}
