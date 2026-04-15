#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "mmtkUnlogBitBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Runtime ////////////////////

void MMTkObjectBarrierSetRuntime::object_probable_write(oop new_obj) const {
  if (mmtk_enable_barrier_fastpath) {
    // Do fast-path check before entering mmtk rust code, to improve mutator performance.
    // This is identical to calling `mmtk_object_probable_write` directly without a fast-path.
    if (is_unlog_bit_set(new_obj)) {
      // Only promoted objects will reach here.
      // The duplicated unlog bit check inside slow-path still remains correct.
      mmtk_object_probable_write((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) new_obj);
    }
  } else {
    // The slow-call will do the unlog bit check again (same as the above fast-path check)
    mmtk_object_probable_write((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) new_obj);
  }
}

void MMTkObjectBarrierSetRuntime::object_reference_write_post(oop src, oop* slot, oop target) const {
  if (mmtk_enable_barrier_fastpath) {
    if (is_unlog_bit_set(src)) {
      object_reference_write_slow_call((void*) src, (void*) slot, (void*) target);
    }
  } else {
    object_reference_write_post_call((void*) src, (void*) slot, (void*) target);
  }
}

//////////////////// C1 ////////////////////

#ifdef COMPILER1

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkObjectBarrierSetC1::object_reference_write_post(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const {
  object_reference_write_pre_or_post(access, src, /* pre = */ false);
}

#undef __
#endif

//////////////////// C2 ////////////////////

#ifdef COMPILER2
#define __ ideal.

void MMTkObjectBarrierSetC2::object_reference_write_post(GraphKit* kit, Node* src, Node* slot, Node* val) const {
  if (can_remove_barrier(kit, &kit->gvn(), src, slot, val, /* skip_const_null */ true)) return;

  MMTkIdealKit ideal(kit, true);

  object_reference_write_pre_or_post(ideal, src, /* pre = */ false);

  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

#undef __
#endif
