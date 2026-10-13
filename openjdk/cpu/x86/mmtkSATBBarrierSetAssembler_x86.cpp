#include "precompiled.hpp"
#include "mmtkSATBBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Assembler ////////////////////

#define __ masm->

void MMTkSATBBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp_thread) {
  BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
  weak_reference_load_barrier(masm, decorators, type, dst, tmp1, uintptr_t(&CONCURRENT_MARKING_ACTIVE));
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
    __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_pre_call), 3);
    __ popa();
    __ bind(done);
  }
}

#undef __
