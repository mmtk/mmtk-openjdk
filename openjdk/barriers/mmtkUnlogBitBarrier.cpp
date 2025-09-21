#include "precompiled.hpp"
#include "mmtkUnlogBitBarrier.hpp"

#include "runtime/interfaceSupport.inline.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"

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

#define __ ce->masm()->

void MMTkUnlogBitBarrierSetAssembler::generate_c1_unlog_bit_barrier_slow_path_stub(LIR_Assembler* ce, MMTkC1UnlogBitBarrierSlowPathStub* stub) const {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());
  ce->store_parameter(stub->src->as_pointer_register(), 0);
  ce->store_parameter(0, 1);
  ce->store_parameter(0, 2);
  CodeBlob* code_blob = stub->pre ? bs->pre_barrier_c1_runtime_code_blob()
                      :             bs->post_barrier_c1_runtime_code_blob();
  __ call(RuntimeAddress(code_blob->code_begin()));
  __ jmp(*stub->continuation());
}

void MMTkC1UnlogBitBarrierSlowPathStub::emit_code(LIR_Assembler* ce) {
  MMTkUnlogBitBarrierSetAssembler* bs = (MMTkUnlogBitBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->generate_c1_unlog_bit_barrier_slow_path_stub(ce, this);
}

#undef __

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkUnlogBitBarrierSetC1::emit_check_unlog_bit_fast_path(LIRGenerator* gen, LIR_Opr src, CodeStub* slow) {
  // We need to do bit operations on the address of `src`. In order to move `src` (`T_OBJECT` or
  // `T_ARRAY`) to a pointer regiseter (`T_LONG` on 64 bit), the source operand must be in
  // register, in which case `LIR_Assembler::reg2reg` works as expected.  Otherwise `stack2ref`
  // will complain that the source (`T_OBJECT` or `T_ARRAY` is single-cpu while the destination
  // `T_LONG` is double-cpu).
  //
  // However, checking `src.is_register()` won't work because the same LIR code may be compiled
  // again. Even it is register the first time, `src.is_stack()` may instead be true at the second
  // time.
  //
  // So we introduce an intermediate step.  We move `src` into `addr` which is a `T_OBJECT`
  // register first to make sure it is in register.  Then we move `addr` to newly created pointer
  // registers.
  LIR_Opr addr = gen->new_register(T_OBJECT);
  __ move(src, addr);
  // uint8_t* meta_addr = (uint8_t*) (side_metadata_base_address() + (addr >> 6));
  LIR_Opr offset = gen->new_pointer_register();
  __ move(addr, offset);
  __ unsigned_shift_right(offset, 6, offset);
  LIR_Opr base = gen->new_pointer_register();
  __ move(LIR_OprFact::longConst(UNLOG_BIT_BASE_ADDRESS), base);
  LIR_Address* meta_addr = new LIR_Address(base, offset, T_BYTE);
  // uint8_t byte_val = *meta_addr;
  LIR_Opr byte_val = gen->new_register(T_INT);
  __ move(meta_addr, byte_val);

  // intptr_t shift = (addr >> 3) & 0b111;
  LIR_Opr shift = gen->new_register(T_INT);
  __ move(addr, shift);
  __ unsigned_shift_right(shift, 3, shift);
  __ logical_and(shift, LIR_OprFact::intConst(0b111), shift);
  // if (((byte_val >> shift) & 1) == 1) slow;
  LIR_Opr result = byte_val;
  __ unsigned_shift_right(result, shift, result, LIR_OprFact::illegalOpr);
  __ logical_and(result, LIR_OprFact::intConst(1), result);
  __ cmp(lir_cond_equal, result, LIR_OprFact::intConst(1));
  __ branch(lir_cond_equal, T_BYTE, slow);
}

void MMTkUnlogBitBarrierSetC1::object_reference_write_pre_or_post(LIRAccess& access, bool pre) {
  LIRGenerator* gen = access.gen();
  DecoratorSet decorators = access.decorators();
  if ((decorators & IN_HEAP) == 0) return;

  LIR_Opr src = access.base().opr();

  CodeStub* slow = new MMTkC1UnlogBitBarrierSlowPathStub(src, pre);

  if (mmtk_enable_barrier_fastpath) {
    emit_check_unlog_bit_fast_path(gen, src, slow);
  } else {
    __ jump(slow);
  }

  __ branch_destination(slow->continuation());
}

#undef __
