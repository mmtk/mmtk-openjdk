#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

#define __ masm->

void MMTkObjectBarrierSetAssembler::object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const {
  assert(false, "Not implemented");
}

void MMTkObjectBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop,
                                  Register src, Register dst, Register count, Register tmp, RegSet saved_regs) {
  // see also void G1BarrierSetAssembler::gen_write_ref_array_post_barrier
  assert_different_registers(src, dst, count);
  const bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if (is_oop && !dest_uninitialized) {
    // in address generate_checkcast_copy, caller tells us to save count
    __ push_reg(saved_regs, sp);
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_post_call), src, dst, count);
    __ pop_reg(saved_regs, sp);
  }
}

#undef __