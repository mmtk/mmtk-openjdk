#include "precompiled.hpp"
#include "mmtkSATBBarrier.hpp"
#include "mmtkMutator.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Assembler ////////////////////

#define __ masm->

void MMTkSATBBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp_thread) {
  bool on_oop = type == T_OBJECT || type == T_ARRAY;
  bool on_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool on_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool on_reference = on_weak || on_phantom;

  BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);

  if (mmtk_enable_reference_load_barrier) {
    if (on_oop && on_reference) {
      Label done;

      assert_different_registers(dst, tmp1);

      // No slow-call if SATB is not active
      // intptr_t tmp1_q = CONCURRENT_MARKING_ACTIVE;
      __ movptr(tmp1, intptr_t(&CONCURRENT_MARKING_ACTIVE));
      // Load with zero extension to 32 bits.
      // uint32_t tmp1_l = (uint32_t)(*(unt8_t*)tmp1_q);
      __ movzbl(tmp1, Address(tmp1, 0));
      // if (tmp1_l == 0) goto done;
      __ testl(tmp1, tmp1);
      __ jcc(Assembler::zero, done);
      // if (dst == 0) goto done;
      __ testptr(dst, dst);
      __ jcc(Assembler::zero, done);
      // Do slow-call: call Rust directly with mutator from thread register
      __ pusha();
      __ mov(c_rarg0, dst);
      Address mutator(r15_thread, in_bytes(JavaThread::third_party_heap_mutator_offset()));
      __ lea(c_rarg1, mutator);
      __ MacroAssembler::call_VM_leaf_base(FN_ADDR(mmtk_load_reference), 2);
      __ popa();
      __ bind(done);
    }
  }
}

void MMTkSATBBarrierSetAssembler::object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) const {
  if (can_remove_barrier(decorators, val, /* skip_const_null */ false)) return;
  object_reference_write_pre_or_post(masm, decorators, dst, val, tmp1, tmp2, tmp3, /* pre = */ true);
}

void MMTkSATBBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  if (type == T_OBJECT || type == T_ARRAY) {
    Label done;
    // Skip the runtime call if count is zero.
    __ testptr(count, count);
    __ jcc(Assembler::zero, done);
    __ pusha();
    __ movptr(c_rarg0, src);
    __ movptr(c_rarg1, dst);
    __ movptr(c_rarg2, count);
    Address mutator(r15_thread, in_bytes(JavaThread::third_party_heap_mutator_offset()));
    __ lea(c_rarg3, mutator);
    __ call_VM_leaf_base(FN_ADDR(mmtk_array_copy_pre), 4);
    __ popa();
    __ bind(done);
  }
}

#undef __
