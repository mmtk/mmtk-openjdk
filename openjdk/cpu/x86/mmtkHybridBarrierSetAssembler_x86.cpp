#include "precompiled.hpp"
#include "mmtkHybridBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Assembler ////////////////////

#define __ masm->

void MMTkHybridBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp_thread) {
  BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
  weak_reference_load_barrier(masm, decorators, type, dst, tmp1, (uintptr_t) hybrid_barrier_active_address());
}

void MMTkHybridBarrierSetAssembler::object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) const {
  // Stores of null need the barrier, too (the old value must be logged).
  if (can_remove_barrier(decorators, val, /* skip_const_null */ false)) return;

  Register base = dst.base();
  Register index = dst.index();
  assert(tmp1 != noreg && tmp2 != noreg, "need two temporary registers");
  assert_different_registers(tmp1, tmp2, c_rarg0, c_rarg1, c_rarg2);
  assert(tmp1 != base && tmp1 != index && tmp1 != val, "tmp1 must not be an input");
  assert(tmp2 != base && tmp2 != index && tmp2 != val, "tmp2 must not be an input");

  Label done;
  if (mmtk_enable_barrier_fastpath) {
    // if (*hybrid_barrier_active_address() == 0) goto done;
    __ movptr(tmp1, (intptr_t) hybrid_barrier_active_address());
    __ cmpb(Address(tmp1, 0), 0);
    __ jcc(Assembler::equal, done);
  }

  // This is a pre-barrier.  Preserve all registers for the actual write operation.
  __ pusha();
  // Compute the arguments in temporaries first, because `base` and `index` may be argument registers.
  __ lea(tmp1, dst);
  if (val == noreg) {
    __ xorptr(tmp2, tmp2);
  } else {
    __ movptr(tmp2, val);
  }
  __ movptr(c_rarg0, base);
  __ movptr(c_rarg1, tmp1);
  __ movptr(c_rarg2, tmp2);
  address entry_point = mmtk_enable_barrier_fastpath ? FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call)
                      :                                FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_pre_call);
  __ call_VM_leaf_base(entry_point, 3);
  __ popa();

  __ bind(done);
}

void MMTkHybridBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  if (!is_reference_type(type)) return;
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;

  Label done;
  // Skip the runtime call if count is zero.
  __ testptr(count, count);
  __ jcc(Assembler::zero, done);
  if (mmtk_enable_barrier_fastpath) {
    // if (*hybrid_barrier_active_address() == 0) goto done;
    // No register is free here, so borrow rax.  `pop` does not change the flags.
    __ push(rax);
    __ movptr(rax, (intptr_t) hybrid_barrier_active_address());
    __ cmpb(Address(rax, 0), 0);
    __ pop(rax);
    __ jcc(Assembler::equal, done);
  }
  __ pusha();
  // Shuffle (src, dst, count) into (c_rarg0, c_rarg1, c_rarg2) through the stack, so that it does
  // not matter which registers they are in.
  __ push(src);
  __ push(dst);
  __ push(count);
  __ pop(c_rarg2);
  __ pop(c_rarg1);
  __ pop(c_rarg0);
  // If the destination is not initialised (e.g. a new array filled by the copy), its old contents
  // must not be read, so only the source is shaded.
  address entry_point = dest_uninitialized ? FN_ADDR(MMTkHybridBarrierSetRuntime::object_reference_array_copy_pre_src_only_call)
                      :                      FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_pre_call);
  __ call_VM_leaf_base(entry_point, 3);
  __ popa();
  __ bind(done);
}

#undef __

//////////////////// Assembler/C1 ////////////////////

#define __ ce->masm()->

void MMTkHybridBarrierSetAssembler::generate_c1_hybrid_barrier_stub(LIR_Assembler* ce, MMTkC1HybridBarrierStub* stub) const {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());

  Register src = stub->src->as_pointer_register();
  // Unsafe stores may have a null base.  They are not heap stores, so skip the barrier.
  __ testptr(src, src);
  __ jcc(Assembler::zero, *stub->continuation());

  ce->store_parameter(src, 0);
  if (stub->patching) {
    // The field offset is not known until the store is patched, so we cannot compute the slot.
    // Call `object_probable_write(src)` instead (see `generate_c1_runtime_stub_general`).
    ce->store_parameter(0, 1);
    ce->store_parameter(0, 2);
    __ call(RuntimeAddress(bs->object_reference_write_pre_c1_runtime_code_blob_with_patch_fix()->code_begin()));
  } else {
    ce->store_parameter(stub->slot->as_pointer_register(), 1);
    ce->store_parameter(stub->new_val->as_pointer_register(), 2);
    CodeBlob* code_blob = mmtk_enable_barrier_fastpath ? bs->object_reference_write_slow_c1_runtime_code_blob()
                        :                                bs->object_reference_write_pre_c1_runtime_code_blob();
    __ call(RuntimeAddress(code_blob->code_begin()));
  }
  __ jmp(*stub->continuation());
}

void MMTkC1HybridBarrierStub::emit_code(LIR_Assembler* ce) {
  MMTkHybridBarrierSetAssembler* bs = (MMTkHybridBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->generate_c1_hybrid_barrier_stub(ce, this);
}

#undef __
