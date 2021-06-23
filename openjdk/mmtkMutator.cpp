
#include "mmtk.h"
#include "mmtkMutator.hpp"
#include <atomic>

MMTkMutatorContext MMTkMutatorContext::bind(::Thread* current) {
    return *((MMTkMutatorContext*) ::bind_mutator((void*) current));
}

HeapWord* MMTkMutatorContext::alloc(size_t bytes, Allocator allocator) {
    static std::atomic<long> slow_count(0);
    static std::atomic<long> large_count(0);

    slow_count.fetch_add(1);
    if (bytes >= get_max_non_los_default_alloc_bytes()) {
        allocator = AllocatorLos;
        large_count.fetch_add(1);
    }

    if (slow_count > 0 && slow_count % 10000 == 0) {
        printf("slowpath alloc: %ld large / %ld total\n", large_count.load(), slow_count.load());
    }

    // FIXME: Proper use of slow-path api
    HeapWord* o = (HeapWord*) ::alloc((MMTk_Mutator) this, bytes, HeapWordSize, 0, allocator);
    // Post allococation. Currently we are only calling post_alloc in slowpath here.
    // TODO: We also need to call them in the fastpath.
    ::post_alloc((MMTk_Mutator) this, o, bytes, allocator);
    return o;
}

void MMTkMutatorContext::flush() {
    ::flush_mutator((MMTk_Mutator) this);
}