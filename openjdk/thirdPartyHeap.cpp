
#include "gc/shared/thirdPartyHeap.hpp"
#include "mmtk.h"
#include "mmtkArguments.hpp"

// GCArguments* mmtk_new_gc_arguments() {
//     return new MMTkArguments();
// }

namespace third_party_heap {

class MutatorContext;

MutatorContext* bind_mutator(::Thread* current) {
    return (MutatorContext*) ::bind_mutator((void*) current);
}

GCArguments* new_gc_arguments() {
    return NULL;
}

};

// #endif