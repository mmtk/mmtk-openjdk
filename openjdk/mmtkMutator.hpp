
#ifndef SHARE_VM_GC_MMTK_MMTKMUTATOR_HPP
#define SHARE_VM_GC_MMTK_MMTKMUTATOR_HPP

#include "mmtk.h"
#include "utilities/globalDefinitions.hpp"
#include "gc/shared/thirdPartyHeap.hpp"

// class Thread;

class MMTkMutatorContext: public third_party_heap::MutatorContext {
    void* _tls;
    char* _cursor;
    char* _limit;
public:
    inline HeapWord* alloc(size_t bytes) {
        bytes = (bytes + (HeapWordSize - 1)) & ~(HeapWordSize - 1);
        if (_cursor + bytes <= _limit) {
            HeapWord* start = (HeapWord*) _cursor;
            _cursor = _cursor + bytes;
            return start;
        } else {
            return (HeapWord*) alloc_slow((MMTk_Mutator) this, bytes, HeapWordSize, 0, 0);
        }
    }
};

#endif