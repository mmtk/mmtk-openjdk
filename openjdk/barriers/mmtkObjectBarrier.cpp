#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

void MMTkObjectBarrierSetRuntime::object_reference_write_post(oop src, oop* slot, oop target) const {
#if MMTK_ENABLE_BARRIER_FASTPATH
  intptr_t addr = (intptr_t) (void*) src;
  uint8_t* meta_addr = (uint8_t*) (SIDE_METADATA_BASE_ADDRESS + (addr >> 6));
  intptr_t shift = (addr >> 3) & 0b111;
  uint8_t byte_val = *meta_addr;
  if (((byte_val >> shift) & 1) == 1) {
    // MMTkObjectBarrierSetRuntime::object_reference_write_pre_slow()((void*) src);
    object_reference_write_slow_call((void*) src, (void*) slot, (void*) target);
  }
#else
  object_reference_write_post_call((void*) src, (void*) slot, (void*) target);
#endif
}

#define __ masm->

void MMTkObjectBarrierSetAssembler::object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const {
  if (can_remove_barrier(decorators, val, /* skip_const_null */ true)) return;
  Register obj = dst.base();
#if MMTK_ENABLE_BARRIER_FASTPATH
  Label done;

  Register tmp3 = rscratch1;
  Register tmp4 = rscratch2;
  assert_different_registers(obj, tmp2, tmp3);
  assert_different_registers(tmp4, rcx);

  // tmp2 = load-byte (SIDE_METADATA_BASE_ADDRESS + (obj >> 6));
  __ movptr(tmp3, obj);
  __ shrptr(tmp3, 6);
  __ movptr(tmp2, SIDE_METADATA_BASE_ADDRESS);
  __ movb(tmp2, Address(tmp2, tmp3));
  // tmp3 = (obj >> 3) & 7
  __ movptr(tmp3, obj);
  __ shrptr(tmp3, 3);
  __ andptr(tmp3, 7);
  // tmp2 = tmp2 >> tmp3
  __ movptr(tmp4, rcx);
  __ movl(rcx, tmp3);
  __ shrptr(tmp2);
  __ movptr(rcx, tmp4);
  // if ((tmp2 & 1) == 1) goto slowpath;
  __ andptr(tmp2, 1);
  __ cmpptr(tmp2, 1);
  __ jcc(Assembler::notEqual, done);

  __ movptr(c_rarg0, obj);
  __ lea(c_rarg1, dst);
  __ movptr(c_rarg2, val == noreg ?  (int32_t) NULL_WORD : val);
  __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), 3);

  __ bind(done);
#else
  __ movptr(c_rarg0, obj);
  __ lea(c_rarg1, dst);
  __ movptr(c_rarg2, val == noreg ?  (int32_t) NULL_WORD : val);
  __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call), 3);
#endif
}

void MMTkObjectBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  const bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if ((type == T_OBJECT || type == T_ARRAY) && !dest_uninitialized) {
    __ pusha();
    __ movptr(c_rarg0, src);
    __ movptr(c_rarg1, dst);
    __ movptr(c_rarg2, count);
    __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_post_call), 3);
    __ popa();
  }
}

#undef __

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkObjectBarrierSetC1::object_reference_write_post(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const {
  LIRGenerator* gen = access.gen();
  DecoratorSet decorators = access.decorators();
  if ((decorators & IN_HEAP) == 0) return;
  if (!src->is_register()) {
    LIR_Opr reg = gen->new_pointer_register();
    if (src->is_constant()) {
      __ move(src, reg);
    } else {
      __ leal(src, reg);
    }
    src = reg;
  }
  assert(src->is_register(), "must be a register at this point");
  if (!slot->is_register()) {
    LIR_Opr reg = gen->new_pointer_register();
    if (slot->is_constant()) {
      __ move(slot, reg);
    } else {
      __ leal(slot, reg);
    }
    slot = reg;
  }
  assert(slot->is_register(), "must be a register at this point");
  if (!new_val->is_register()) {
    LIR_Opr new_val_reg = gen->new_register(T_OBJECT);
    if (new_val->is_constant()) {
      __ move(new_val, new_val_reg);
    } else {
      __ leal(new_val, new_val_reg);
    }
    new_val = new_val_reg;
  }
  assert(new_val->is_register(), "must be a register at this point");
  CodeStub* slow = new MMTkC1BarrierStub(src, slot, new_val);

#if MMTK_ENABLE_BARRIER_FASTPATH
  LIR_Opr addr = src;
  // uint8_t* meta_addr = (uint8_t*) (SIDE_METADATA_BASE_ADDRESS + (addr >> 6));
  LIR_Opr offset = gen->new_pointer_register();
  __ move(addr, offset);
  __ unsigned_shift_right(offset, 6, offset);
  LIR_Opr base = gen->new_pointer_register();
  __ move(LIR_OprFact::longConst(SIDE_METADATA_BASE_ADDRESS), base);
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
#else
  __ jump(slow);
#endif

  __ branch_destination(slow->continuation());
}

#undef __

#define __ ideal.

void MMTkObjectBarrierSetC2::object_reference_write_post(GraphKit* kit, Node* src, Node* slot, Node* val) const {
  if (can_remove_barrier(kit, &kit->gvn(), src, slot, val, /* skip_const_null */ true)) return;

  MMTkIdealKit ideal(kit, true);

#if MMTK_ENABLE_BARRIER_FASTPATH
  Node* no_base = __ top();
  float unlikely  = PROB_UNLIKELY(0.999);

  Node* zero  = __ ConI(0);
  Node* addr = __ CastPX(__ ctrl(), src);
  Node* meta_addr = __ AddP(no_base, __ ConP(SIDE_METADATA_BASE_ADDRESS), __ URShiftX(addr, __ ConI(6)));
  Node* byte = __ load(__ ctrl(), meta_addr, TypeInt::INT, T_BYTE, Compile::AliasIdxRaw);
  Node* shift = __ URShiftX(addr, __ ConI(3));
  shift = __ AndI(__ ConvL2I(shift), __ ConI(7));
  Node* result = __ AndI(__ URShiftI(byte, shift), __ ConI(1));

  __ if_then(result, BoolTest::ne, zero, unlikely); {
    const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM);
    Node* x = __ make_leaf_call(tf, FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), "mmtk_barrier_call", src, slot, val);
  } __ end_if();
#else
  const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM);
  Node* x = __ make_leaf_call(tf, FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call), "mmtk_barrier_call", src, slot, val);
#endif

  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

#undef __
