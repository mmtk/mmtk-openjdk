#define private public // too lazy to change openjdk... (needs LIR_Assembler::deoptimize_trap)
#define protected public
#include "mmtk.h"
#include "mmtkFieldBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Assembler ////////////////////

#define __ masm->

void MMTkFieldBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp2) {
  bool on_oop = type == T_OBJECT || type == T_ARRAY;
  bool on_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool on_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool on_reference = on_weak || on_phantom;
  BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp2);
#if SOFT_REFERENCE_LOAD_BARRIER
  if (on_oop && on_reference) {
    Label done;

    assert_different_registers(dst, tmp1);

    // No slow-call if SATB is not active
    // intptr_t tmp1_q = CONCURRENT_MARKING_ACTIVE;
    __ movptr(tmp1, intptr_t(&CONCURRENT_MARKING_ACTIVE));
    // Load with zero extension to 32 bits.
    // uint32_t tmp1_l = (uint32_t)(*(unt8_t*)tmp1_q);
    __ ldrb(tmp1, Address(tmp1, 0));
    // if (tmp1_l == 0) goto done;
    __ cbz(tmp1, done);
    // if (dst == 0) goto done;
    __ cbz(dst, done);
    // Do slow-call
    // LR may be live (e.g. in the interpreter's Reference.get entry), and it is not saved by
    // push_call_clobbered_registers. Save it (and FP) around the call like G1BarrierSetAssembler::load_at.
    __ enter(/*strip_ret_addr*/true);
    __ push_call_clobbered_registers();
    __ mov(c_rarg0, dst);
    __ MacroAssembler::call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::load_reference_call), 1);
    __ pop_call_clobbered_registers();
    __ leave();
    __ bind(done);
  }
#endif
}

// Set up the arguments (src, slot, val) of the write barrier slow-call.
// Unlike x86, the argument registers may alias dst.base(), dst.index() or val. For example, the
// template interpreter's putfield stores to Address(r2, r19) with val in r0, i.e. dst.base() is
// c_rarg2 and val is c_rarg0. So we compute the arguments into the scratch registers first.
static void setup_write_barrier_args(MacroAssembler* masm, Address dst, Register val) {
  assert_different_registers(dst.base(), dst.index(), rscratch1, rscratch2);
  assert(val != rscratch1 && val != rscratch2, "must not clobber the value register");
  __ lea(rscratch1, dst);
  if (val == noreg)
    __ mov(rscratch2, zr);
  else
    __ mov(rscratch2, val);
  __ mov(c_rarg0, dst.base());
  __ mov(c_rarg1, rscratch1);
  __ mov(c_rarg2, rscratch2);
}

void MMTkFieldBarrierSetAssembler::object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) const {
  if (can_remove_barrier(decorators, val, /* skip_const_null */ false)) return;
  if (mmtk_enable_barrier_fastpath) {
    Label done;

    assert_different_registers(tmp1, tmp2,  dst.base(),  dst.index());

    // tmp2 = load-byte (side_metadata_base_address() + (obj >> 6));
    __ lea(tmp1, dst);
    __ lsr(tmp1, tmp1, UseCompressedOops ? 5 : 6);
    __ mov(tmp2, (uint64_t)side_metadata_base_address());
    __ ldrb(tmp2, Address(tmp2, tmp1));
    __ cbz(tmp2, done);
    // tmp1 = (obj >> 3) & 7
    __ lea(tmp1, dst);
    __ lsr(tmp1, tmp1, UseCompressedOops ? 2 : 3);
    __ andr(tmp1, tmp1, 7);
    // tmp2 = tmp2 >> tmp1
    __ lsrv(tmp2, tmp2, tmp1);
    // if ((tmp2 & 1) == 1) goto slowpath;
    __ andr(tmp2, tmp2, 1);
    __ cmp(tmp2, (u1)kUnloggedValue);
    __ br(Assembler::NE, done);

    // TODO: Spill fewer registers
    __ push_call_clobbered_registers();
    setup_write_barrier_args(masm, dst, val);
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), 3);
    __ pop_call_clobbered_registers();

    __ bind(done);
  } else {
    // aarch64 has no pusha/popa. Saving the call-clobbered registers is enough because the
    // callee-saved registers are preserved by the call.
    __ push_call_clobbered_registers();
    setup_write_barrier_args(masm, dst, val);
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_pre_call), 3);
    __ pop_call_clobbered_registers();
  }
}

void MMTkFieldBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop, Register src, Register dst, Register count, RegSet saved_regs) {
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if (dest_uninitialized) return;
  if (is_oop) {
    Label done;
    // Bailout if count is zero
    __ cbz(count, done);
    __ push_call_clobbered_registers();
    assert_different_registers(c_rarg0, dst, count);
    assert_different_registers(c_rarg1, count);
    if (c_rarg0 != src)   __ mov(c_rarg0, src);
    if (c_rarg1 != dst)   __ mov(c_rarg1, dst);
    if (c_rarg2 != count) __ mov(c_rarg2, count);
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_pre_call), 3);
    __ pop_call_clobbered_registers();
    __ bind(done);
  }
}

#undef __
#define __ ce->masm()->

void MMTkFieldBarrierSetAssembler::generate_c1_pre_write_barrier_stub(LIR_Assembler* ce, MMTkC1FieldBarrierStub* stub) const {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());

  // For pre-barriers, stub->slot may not be a resolved address.
  address runtime_address;
  if (stub->patch_code != lir_patch_none) {
    // Unlike x86, C1 on aarch64 does not patch unresolved field accesses. It deoptimizes instead
    // (see LIR_Assembler::mem2reg), and the interpreter will execute the store with its own barrier.
    // So we do not need the "with_patch_fix" runtime stub here.
    ce->deoptimize_trap(stub->info);
    __ b(*stub->continuation());
    return;
  } else {
    // Store parameter
    ce->store_parameter(stub->slot->as_pointer_register(), 1);
    runtime_address = bs->object_reference_write_pre_c1_runtime_code_blob()->code_begin();
  }

  ce->store_parameter(stub->src->as_pointer_register(), 0);
  ce->store_parameter(stub->new_val->as_pointer_register(), 2);
  __ far_call(RuntimeAddress(runtime_address));
  __ b(*stub->continuation());
}

#undef __
