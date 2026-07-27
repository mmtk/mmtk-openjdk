#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Assembler ////////////////////

#define __ masm->

void MMTkGenBarrierSetAssembler::object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) const {
  if (can_remove_barrier(decorators, val, /* skip_const_null */ true)) return;
  object_reference_write_pre_or_post(masm, decorators, dst, val, tmp1, tmp2, tmp3, /* pre = */ false);
}

void MMTkGenBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop,
                                  Register src, Register dst, Register count, Register tmp, RegSet saved_regs) {
  // see also void G1BarrierSetAssembler::gen_write_ref_array_post_barrier
  assert_different_registers(src, dst, count);
  // const bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  // if (is_oop && !dest_uninitialized) {
  if (is_oop){
    __ push(saved_regs, sp);
    __ mov(c_rarg1, dst);
    __ mov(c_rarg2, count);
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_post_call), 3);
    __ pop(saved_regs, sp);
  }
}


#undef __
