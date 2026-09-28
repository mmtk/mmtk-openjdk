#define private public // too lazy to change openjdk... (needs LIR_Assembler::mem2reg and as_Address)
#define protected public
#include "mmtk.h"
#include "mmtkFieldBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Assembler ////////////////////

#define __ masm->

void MMTkFieldBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp_thread) {
  bool on_oop = type == T_OBJECT || type == T_ARRAY;
  bool on_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool on_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool on_reference = on_weak || on_phantom;
  BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
#if SOFT_REFERENCE_LOAD_BARRIER
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
    // Do slow-call
    __ push_call_clobbered_registers(false /* save_fpu */);
    __ mov(c_rarg0, dst);
    __ MacroAssembler::call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::load_reference_call), 1);
    __ pop_call_clobbered_registers(false /* save_fpu */);
    __ bind(done);
  }
#endif
}

void MMTkFieldBarrierSetAssembler::object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) const {
  if (can_remove_barrier(decorators, val, /* skip_const_null */ false)) return;
  if (mmtk_enable_barrier_fastpath) {
    Label done;

    assert_different_registers(tmp1, tmp2,  dst.base(),  dst.index());
    assert_different_registers(rcx, tmp2);

    // tmp2 = load-byte (side_metadata_base_address() + (obj >> 6));
    __ lea(tmp1, dst);
    __ shrptr(tmp1, UseCompressedOops ? 5 : 6);
    __ movptr(tmp2, side_metadata_base_address());
    __ movzbl(tmp2, Address(tmp2, tmp1));
    __ cmpl(tmp2, 0);
    __ jcc(Assembler::equal, done);
    // tmp1 = (obj >> 3) & 7
    __ lea(tmp1, dst);
    __ shrptr(tmp1, UseCompressedOops ? 2 : 3);
    __ andptr(tmp1, 7);
    // tmp2 = tmp2 >> tmp1
    __ xchgptr(tmp1, rcx);
    __ shrptr(tmp2);
    __ xchgptr(tmp1, rcx);
    // if ((tmp2 & 1) == 1) goto slowpath;
    __ andptr(tmp2, 1);
    __ cmpptr(tmp2, kUnloggedValue);
    __ jcc(Assembler::notEqual, done);

    // TODO: Spill fewer registers
    __ push_call_clobbered_registers(false /* save_fpu */);
    __ movptr(c_rarg0, dst.base());
    __ lea(c_rarg1, dst);
    if (val == noreg)
      __ movptr(c_rarg2, NULL_WORD);
    else
      __ movptr(c_rarg2, val);
    __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), 3);
    __ pop_call_clobbered_registers(false /* save_fpu */);

    __ bind(done);
  } else {
    __ pusha();
    __ movptr(c_rarg0, dst.base());
    __ lea(c_rarg1, dst);
    if (val == noreg)
      __ movptr(c_rarg2, NULL_WORD);
    else
      __ movptr(c_rarg2, val);
    __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_pre_call), 3);
    __ popa();
  }
}

void MMTkFieldBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if (dest_uninitialized) return;
  if (type == T_OBJECT || type == T_ARRAY) {
    Label done;
    // Bailout if count is zero
    __ cmpptr(count, 0);
    __ jcc(Assembler::equal, done);
    __ push_call_clobbered_registers(false /* save_fpu */);
    assert_different_registers(c_rarg0, dst, count);
    assert_different_registers(c_rarg1, count);
    if (c_rarg0 != src)   __ movptr(c_rarg0, src);
    if (c_rarg1 != dst)   __ movptr(c_rarg1, dst);
    if (c_rarg2 != count) __ movptr(c_rarg2, count);
    __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_pre_call), 3);
    __ pop_call_clobbered_registers(false /* save_fpu */);
    __ bind(done);
  }
}


#undef __
#define __ ce->masm()->

void MMTkFieldBarrierSetAssembler::generate_c1_pre_write_barrier_stub(LIR_Assembler* ce, MMTkC1FieldBarrierStub* stub) const {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());

  // For pre-barriers, stub->slot may not be a resolved address.
  // Manually patch the address and goes to the slow-path unconditionally.
  address runtime_address;
  if (stub->patch_code != lir_patch_none) {
    // Patch
    assert(stub->scratch->is_single_cpu(), "must be");
    assert(stub->scratch->is_register(), "Precondition.");
    ce->mem2reg(stub->slot, stub->scratch, T_OBJECT, stub->patch_code, stub->info, false /*wide*/);
    // Resolve address
    auto masm = ce->masm();
    LIR_Address* addr = stub->slot->as_address_ptr();
    Address from_addr = ce->as_Address(addr);
    __ lea(stub->scratch->as_register(), from_addr);
    // Store parameter
    ce->store_parameter(stub->scratch->as_pointer_register(), 1);
    runtime_address = bs->object_reference_write_pre_c1_runtime_code_blob_with_patch_fix()->code_begin();
  } else {
    // Store parameter
    ce->store_parameter(stub->slot->as_pointer_register(), 1);
    runtime_address = bs->object_reference_write_pre_c1_runtime_code_blob()->code_begin();
  }

  ce->store_parameter(stub->src->as_pointer_register(), 0);
  ce->store_parameter(stub->new_val->as_pointer_register(), 2);
  __ call(RuntimeAddress(runtime_address));
  __ jmp(*stub->continuation());
}

#undef __
