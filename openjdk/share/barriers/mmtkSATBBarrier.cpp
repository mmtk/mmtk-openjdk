#include "precompiled.hpp"
#include "mmtkSATBBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Runtime ////////////////////

void MMTkSATBBarrierSetRuntime::load_reference(DecoratorSet decorators, oop value) const {
  if (mmtk_enable_reference_load_barrier) {
    if (CONCURRENT_MARKING_ACTIVE == 1 && value != NULL)
      ::mmtk_load_reference((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) value);
  }
};

void MMTkSATBBarrierSetRuntime::object_probable_write(oop new_obj) const {
  // We intentionally leave this method blank.
  // This method is called after slowpath allocation exits.
  // Because the new_obj is just allocated,
  // it does not have any fields holding old values for the SATB barrier to remember.
}

void MMTkSATBBarrierSetRuntime::object_reference_write_pre(oop src, oop* slot, oop target) const {
  if (mmtk_enable_barrier_fastpath) {
    if (is_unlog_bit_set(src)) {
      object_reference_write_slow_call((void*) src, (void*) slot, (void*) target);
    }
  } else {
    object_reference_write_pre_call((void*) src, (void*) slot, (void*) target);
  }
}

//////////////////// C1 ////////////////////

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkSATBBarrierSetC1::load_at_resolved(LIRAccess& access, LIR_Opr result) {
  weak_reference_load_at_resolved(access, result, uintptr_t(&CONCURRENT_MARKING_ACTIVE));
}

void MMTkSATBBarrierSetC1::object_reference_write_pre(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const {
  object_reference_write_pre_or_post(access, src, /* pre = */ true);
}

#undef __

//////////////////// C2 ////////////////////

#define __ ideal.

void MMTkSATBBarrierSetC2::object_reference_write_pre(GraphKit* kit, Node* src, Node* slot, Node* val) const {
  if (can_remove_barrier(kit, &kit->gvn(), src, slot, val, /* skip_const_null */ false)) return;

  MMTkIdealKit ideal(kit, true);

  object_reference_write_pre_or_post(ideal, src, /* pre = */ true);

  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

Node* MMTkSATBBarrierSetC2::load_at_resolved(C2Access& access, const Type* val_type) const {
  return weak_reference_load_at_resolved(access, val_type, uintptr_t(&CONCURRENT_MARKING_ACTIVE));
}

#undef __
