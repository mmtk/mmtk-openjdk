#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

#define __ masm->

void MMTkObjectBarrierSetAssembler::object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const {
  // tmp1 and tmp2 is from MacroAssembler::access_store_at
  // For do_oop_store, we have three tmps, x28/t3, x29/t4, x13/a3
  // printf("object_reference_write_post\n");
//   if (can_remove_barrier(decorators, val, /* skip_const_null */ true)) return;
  if (can_remove_barrier(decorators, val, /* skip_const_null */ true)) return;
  Register obj = dst.base();

  if (mmtk_enable_barrier_fastpath) {
    Label done;

    assert_different_registers(obj, tmp1, tmp2);
    assert_different_registers(val, tmp1, tmp2);
    assert(tmp1->is_valid(), "need temp reg");
    assert(tmp2->is_valid(), "need temp reg");
    // tmp1 = load-byte (SIDE_METADATA_BASE_ADDRESS + (obj >> 6));
    __ mov(tmp1, obj);
    __ lsr(tmp1, tmp1, 6); // tmp1 = obj >> 6;
    __ mov(tmp2, SIDE_METADATA_BASE_ADDRESS);
    __ add(tmp1, tmp1, tmp2); // tmp1 = SIDE_METADATA_BASE_ADDRESS + (obj >> 6);
    __ ldrb(tmp1, Address(tmp1, 0));
    // tmp2 = (obj >> 3) & 7
    __ mov(tmp2, obj);
    __ lsr(tmp2, tmp2, 3);
    __ andr(tmp2, tmp2, 7);
    // tmp1 = tmp1 >> tmp2
    __ lsrv(tmp1, tmp1, tmp2);
    // if ((tmp1 & 1) == 1) fall through to slowpath;
    // equivalently ((tmp1 & 1) == 0) go to done
    __ andr(tmp1, tmp1, 1);
    __ cbz(tmp1, done);
    // setup calling convention
    __ mov(c_rarg0, obj);
    __ lea(c_rarg1, dst);
    __ mov(c_rarg2, val == noreg ? zr : val);
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), 3);

    __ bind(done);
  } else {
    __ mov(c_rarg0, obj);
    __ lea(c_rarg1, dst);
    __ mov(c_rarg2, val == noreg ? zr : val);
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call), 3);
  }
}

void MMTkObjectBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop,
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
