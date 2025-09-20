#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

void MMTkObjectBarrierSetRuntime::object_probable_write(oop new_obj) const {
  if (mmtk_enable_barrier_fastpath) {
    // Do fast-path check before entering mmtk rust code, to improve mutator performance.
    // This is identical to calling `mmtk_object_probable_write` directly without a fast-path.
    if (is_unlog_bit_set(new_obj)) {
      // Only promoted objects will reach here.
      // The duplicated unlog bit check inside slow-path still remains correct.
      mmtk_object_probable_write((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) new_obj);
    }
  } else {
    // The slow-call will do the unlog bit check again (same as the above fast-path check)
    mmtk_object_probable_write((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) new_obj);
  }
}

void MMTkObjectBarrierSetRuntime::object_reference_write_post(oop src, oop* slot, oop target) const {
  if (mmtk_enable_barrier_fastpath) {
    if (is_unlog_bit_set(src)) {
      object_reference_write_slow_call((void*) src, (void*) slot, (void*) target);
    }
  } else {
    object_reference_write_post_call((void*) src, (void*) slot, (void*) target);
  }
}

#define __ masm->

void MMTkObjectBarrierSetAssembler::object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, bool compensate_val_reg) const {
  if (can_remove_barrier(decorators, val, /* skip_const_null */ true)) return;
  object_reference_write_pre_or_post(masm, decorators, dst, val, /* pre = */ false);
}

void MMTkObjectBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  // `count` or `dst` register values may get overwritten after the array copy, and `arraycopy_epilogue` can receive invalid addresses.
  // Save the register values here and restore them in `arraycopy_epilogue`.
  // See https://github.com/openjdk/jdk/blob/jdk-11%2B19/src/hotspot/cpu/x86/gc/shared/modRefBarrierSetAssembler_x86.cpp#L37-L50
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool obj_int = type == T_OBJECT LP64_ONLY(&& UseCompressedOops);
  if (type == T_OBJECT || type == T_ARRAY) {
    if (!checkcast) {
      if (!obj_int) {
        // Save count for barrier
        __ movptr(r11, count);
      } else if (disjoint) {
        // Save dst in r11 in the disjoint case
        __ movq(r11, dst);
      }
    }
  }
}

void MMTkObjectBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool obj_int = type == T_OBJECT LP64_ONLY(&& UseCompressedOops);
  const bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if ((type == T_OBJECT || type == T_ARRAY) && !dest_uninitialized) {
    if (!checkcast) {
      if (!obj_int) {
        // Save count for barrier
        count = r11;
      } else if (disjoint) {
        // Use the saved dst in the disjoint case
        dst = r11;
      }
    }
    __ pusha();
    __ movptr(c_rarg0, src);
    __ movptr(c_rarg1, dst);
    __ movptr(c_rarg2, count);
    __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_post_call), 3);
    __ popa();
  }
}

#undef __

#define __ ce->masm()->

void MMTkObjectBarrierSetAssembler::generate_c1_post_write_barrier_stub(LIR_Assembler* ce, MMTkC1PostBarrierStub* stub) const {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());
  ce->store_parameter(stub->src->as_pointer_register(), 0);
  ce->store_parameter(stub->slot->as_pointer_register(), 1);
  ce->store_parameter(stub->new_val->as_pointer_register(), 2);
  __ call(RuntimeAddress(bs->post_barrier_c1_runtime_code_blob()->code_begin()));
  __ jmp(*stub->continuation());
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
  CodeStub* slow = new MMTkC1PostBarrierStub(src, slot, new_val);

  if (mmtk_enable_barrier_fastpath) {
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
  } else {
    __ jump(slow);
  }

  __ branch_destination(slow->continuation());
}

#undef __

#define __ ideal.

void MMTkObjectBarrierSetC2::object_reference_write_post(GraphKit* kit, Node* src, Node* slot, Node* val) const {
  if (can_remove_barrier(kit, &kit->gvn(), src, slot, val, /* skip_const_null */ true)) return;

  MMTkIdealKit ideal(kit, true);

  if (mmtk_enable_barrier_fastpath) {
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
  } else {
    const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM);
    Node* x = __ make_leaf_call(tf, FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call), "mmtk_barrier_call", src, slot, val);
  }

  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

#undef __
