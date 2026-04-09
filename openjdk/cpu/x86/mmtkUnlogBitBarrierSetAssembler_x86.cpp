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

void MMTkUnlogBitBarrierSetAssembler::object_reference_write_pre_or_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3, bool pre) {
  Label done;
  Register obj = dst.base();
  if (mmtk_enable_barrier_fastpath) {
    assert_different_registers(dst.base(), dst.index(), val, tmp1, tmp2, tmp3);

    emit_check_unlog_bit_fast_path(masm, done, obj, tmp1, tmp2, tmp3);
  }

  if (pre) {
    // This is a pre-barrier.  Preserve caller-saved regs for the actual write operation.
    __ pusha();
  }

  __ movptr(c_rarg0, obj);
  // Neither the ObjectBarrier nor the SATBBarrier need to know the slot or the value.
  // We just set both args to nullptr.
  __ xorptr(c_rarg1, c_rarg1);
  __ xorptr(c_rarg2, c_rarg2);
  // Load mutator from thread-local storage and call Rust directly.
  Address mutator(r15_thread, in_bytes(JavaThread::third_party_heap_mutator_offset()));
  __ lea(c_rarg3, mutator);

  address entry_point = mmtk_enable_barrier_fastpath ? FN_ADDR(mmtk_object_reference_write_slow)
                      : pre                          ? FN_ADDR(mmtk_object_reference_write_pre)
                      :                                FN_ADDR(mmtk_object_reference_write_post);

  __ call_VM_leaf_base(entry_point, 4);

  if (pre) {
    __ popa();
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
  __ call(RuntimeAddress(code_blob->code_begin()));
  __ jmp(*stub->continuation());
}

void MMTkC1UnlogBitBarrierSlowPathStub::emit_code(LIR_Assembler* ce) {
  MMTkUnlogBitBarrierSetAssembler* bs = (MMTkUnlogBitBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->generate_c1_unlog_bit_barrier_slow_path_stub(ce, this);
}

#undef __
