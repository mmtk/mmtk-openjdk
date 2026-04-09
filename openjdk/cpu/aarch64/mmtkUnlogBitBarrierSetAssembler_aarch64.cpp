#include "precompiled.hpp"

#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "mmtkMutator.hpp"

#include "utilities/macros.hpp"
#include CPU_HEADER(mmtkUnlogBitBarrierSetAssembler)

#include <cstdint>

//////////////////// Assembler ////////////////////

#define __ masm->

void MMTkUnlogBitBarrierSetAssembler::emit_check_unlog_bit_fast_path(MacroAssembler* masm, Label &done, Register obj, Register tmp1, Register tmp2, Register tmp3) {
  // Note that `tmp1` and `tmp2` are actual temporary registers available for use,
  // not the `tmp1` and `tmp2` from `store_at`.
  assert_different_registers(obj, tmp1, tmp2, tmp3);

  // tmp2 = load-byte (UNLOG_BIT_BASE_ADDRESS + (obj >> 6));
  __ movptr(tmp1, (intptr_t)UNLOG_BIT_BASE_ADDRESS);
  __ add(tmp2, tmp1, obj, Assembler::LSR, 6);
  // tmp1 = (obj >> 3) & 7
  __ movz(tmp1, 7);
  __ andr(tmp1, tmp1, obj, Assembler::LSR, 3);
  // tmp2 = tmp2 >> tmp1
  __ lsrv(tmp2, tmp2, tmp1);
  // if ((tmp2 & (1 << 0)) == 0) goto done;
  __ tbz(tmp2, 0, done);
}

#undef __

#define __ masm->

void MMTkUnlogBitBarrierSetAssembler::object_reference_write_pre_or_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3, bool pre) {
  Label done;
  Register obj = dst.base();
  if (mmtk_enable_barrier_fastpath) {
    assert_different_registers(dst.base(), dst.index(), val, tmp1, tmp2, tmp3);

    emit_check_unlog_bit_fast_path(masm, done, obj, tmp1, tmp2, tmp3);
  }

  if (pre) {
    // This is a pre-barrier.  Preserve caller-saved regs for the actual write operation.
    __ push_call_clobbered_registers();
  }

  __ mov(c_rarg0, obj);
  // Neither the ObjectBarrier nor the SATBBarrier need to know the slot or the value.
  // We just set both args to nullptr.
  __ mov(c_rarg1, zr);
  __ mov(c_rarg2, zr);
  // Load mutator from thread-local storage and call Rust directly.
  __ lea(c_rarg3, Address(rthread, in_bytes(JavaThread::third_party_heap_mutator_offset())));

  address entry_point = mmtk_enable_barrier_fastpath ? FN_ADDR(mmtk_object_reference_write_slow)
                      : pre                          ? FN_ADDR(mmtk_object_reference_write_pre)
                      :                                FN_ADDR(mmtk_object_reference_write_post);

  __ call_VM_leaf(entry_point, 4);

  if (pre) {
    __ pop_call_clobbered_registers();
  }

  if (mmtk_enable_barrier_fastpath) {
    __ bind(done);
  }
}

#undef __

//////////////////// Assembler/C1 ////////////////////

#define __ ce->masm()->

void MMTkUnlogBitBarrierSetAssembler::generate_c1_unlog_bit_barrier_slow_path_stub(LIR_Assembler* ce, MMTkC1UnlogBitBarrierSlowPathStub* stub) const {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());
  ce->store_parameter(stub->src->as_pointer_register(), 0);
  ce->store_parameter(0, 1);
  ce->store_parameter(0, 2);
  CodeBlob* code_blob = stub->fast_path_enabled ? bs->object_reference_write_slow_c1_runtime_code_blob()
                      : stub->pre               ? bs->object_reference_write_pre_c1_runtime_code_blob()
                      :                           bs->object_reference_write_post_c1_runtime_code_blob();
  __ far_call(RuntimeAddress(code_blob->code_begin()));
  __ b(*stub->continuation());
}

void MMTkC1UnlogBitBarrierSlowPathStub::emit_code(LIR_Assembler* ce) {
  MMTkUnlogBitBarrierSetAssembler* bs = (MMTkUnlogBitBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->generate_c1_unlog_bit_barrier_slow_path_stub(ce, this);
}

#undef __
