#include "precompiled.hpp"
#include "mmtkUnlogBitBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

#define __ masm->

void MMTkUnlogBitBarrierSetAssembler::emit_check_unlog_bit_fast_path(MacroAssembler* masm, Label &done, Register obj, Register tmp1, Register tmp2) {
  assert_different_registers(obj, tmp1, tmp2);

  // tmp2 = load-byte (UNLOG_BIT_BASE_ADDRESS + (obj >> 6));
  __ movptr(tmp1, obj);
  __ shrptr(tmp1, 6);
  __ movptr(tmp2, (intptr_t)UNLOG_BIT_BASE_ADDRESS);
  __ movb(tmp2, Address(tmp2, tmp1));
  // tmp1 = (obj >> 3) & 7
  __ movptr(tmp1, obj);
  __ shrptr(tmp1, 3);
  __ andptr(tmp1, 7);
  // tmp2 = tmp2 >> tmp1
  __ xchgptr(tmp1, rcx);
  __ shrptr(tmp2);
  __ xchgptr(tmp1, rcx);
  // if ((tmp2 & 1) == 0) goto done;
  __ testptr(tmp2, 1);
  __ jcc(Assembler::zero, done);
}

#undef __

#define __ masm->

void MMTkUnlogBitBarrierSetAssembler::object_reference_write_pre_or_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, bool pre) {
  Label done;
  Register obj = dst.base();
  if (mmtk_enable_barrier_fastpath) {
    // For some instructions in the template table, such as aastore,
    // we observed that in BarrierSetAssembler::store_at
    // which calls `object_reference_write_pre` or `object_reference_write_post`,
    // dst.base() == tmp1 && dst.index() == tmp2.
    // We can't overwrite those registers,
    // so we don't use tmp1 or tmp2 passed to store_at.
    // Instead, we steal two scratch register to use.
    Register tmp3 = rscratch1;
    Register tmp4 = rscratch2;
    assert_different_registers(dst.base(), dst.index(), val, tmp3, tmp4);

    emit_check_unlog_bit_fast_path(masm, done, obj, tmp3, tmp4);
  }

  if (pre) {
    // This is a pre-barrier.  Preserve caller-saved regs for the actual write operation.
    __ pusha();
  }

  __ movptr(c_rarg0, obj);
  // Neither the ObjectBarrier nor the SATBBarrier need to know the slot or the value.
  // We just set both args to nullptr.
  // We may need to pass actual arguments if we support other barriers.
  //
  // Note: If the `compensate_val_reg` parameter in the post barrier is true, and we are using
  // compressed oops, the `val` register will be holding a compressed pointer to the target object
  // due to the way `BarrierSetAssembler::store_at` works. If the write barrier needs to know the
  // target, we will need to decompress it before passing it to the barrier slow path.
  __ xorptr(c_rarg1, c_rarg1);
  __ xorptr(c_rarg2, c_rarg2);

  address entry_point = mmtk_enable_barrier_fastpath ? FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call)
                      : pre                          ? FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_pre_call)
                      :                                FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call);

  __ call_VM_leaf_base(entry_point, 3);

  if (pre) {
    __ popa();
  }

  if (mmtk_enable_barrier_fastpath) {
    __ bind(done);
  }
}

#undef __

#define __ sasm->

void MMTkUnlogBitBarrierSetAssembler::generate_c1_pre_or_post_write_barrier_runtime_stub(StubAssembler* sasm, const char* name, bool pre) {
  __ prologue(name, false);

  Label done, runtime;

  __ push(c_rarg0);
  __ push(c_rarg1);
  __ push(c_rarg2);
  __ push(rax);

  __ load_parameter(0, c_rarg0);
  __ load_parameter(1, c_rarg1);
  __ load_parameter(2, c_rarg2);

  __ bind(runtime);

  __ save_live_registers_no_oop_map(true);

  address entry_point = mmtk_enable_barrier_fastpath ? FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call)
                      : pre                          ? FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_pre_call)
                      :                                FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call);

  __ call_VM_leaf_base(entry_point, 3);

  __ restore_live_registers(true);

  __ bind(done);
  __ pop(rax);
  __ pop(c_rarg2);
  __ pop(c_rarg1);
  __ pop(c_rarg0);

  __ epilogue();
}
