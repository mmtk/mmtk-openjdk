#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
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

//////////////////// Assembler ////////////////////

#define __ masm->

void MMTkObjectBarrierSetAssembler::object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, bool compensate_val_reg) const {
  if (can_remove_barrier(decorators, val, /* skip_const_null */ true)) return;
  object_reference_write_pre_or_post(masm, decorators, dst, val, /* pre = */ false);
}

void MMTkObjectBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  // `count` or `dst` register values may get overwritten after the array copy, and `arraycopy_epilogue` can receive invalid addresses.
  // Save the register values here and restore them in `arraycopy_epilogue`.
  // See https://github.com/openjdk/jdk/blob/jdk-11%2B19/src/hotspot/cpu/x86/gc/shared/modRefBarrierSetAssembler_x86.cpp#L37-L50
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool obj_int = type == T_OBJECT LP64_ONLY(&& UseCompressedOops);
  if (type == T_OBJECT || type == T_ARRAY) {
    if (!checkcast) {
      if (!obj_int) {
        // Save count for barrier
        __ movptr(r11, count);
      } else if (disjoint) {
        // Save dst in r11 in the disjoint case
        __ movq(r11, dst);
      }
    }
  }
}

void MMTkObjectBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool obj_int = type == T_OBJECT LP64_ONLY(&& UseCompressedOops);
  const bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if ((type == T_OBJECT || type == T_ARRAY) && !dest_uninitialized) {
    if (!checkcast) {
      if (!obj_int) {
        // Save count for barrier
        count = r11;
      } else if (disjoint) {
        // Use the saved dst in the disjoint case
        dst = r11;
      }
    }
    __ pusha();
    __ movptr(c_rarg0, src);
    __ movptr(c_rarg1, dst);
    __ movptr(c_rarg2, count);
    __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_post_call), 3);
    __ popa();
  }
}

#undef __

//////////////////// C1 ////////////////////

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkObjectBarrierSetC1::object_reference_write_post(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const {
  object_reference_write_pre_or_post(access, src, /* pre = */ false);
}

#undef __

//////////////////// C2 ////////////////////

#define __ ideal.

void MMTkObjectBarrierSetC2::object_reference_write_post(GraphKit* kit, Node* src, Node* slot, Node* val) const {
  if (can_remove_barrier(kit, &kit->gvn(), src, slot, val, /* skip_const_null */ true)) return;

  MMTkIdealKit ideal(kit, true);

  object_reference_write_pre_or_post(ideal, src, /* pre = */ false);

  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

#undef __
