#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

void MMTkObjectBarrierSetRuntime::object_probable_write(oop new_obj) const {
  if (mmtk_enable_barrier_fastpath) {
    // Do fast-path check before entering mmtk rust code, to improve mutator performance.
    // This is identical to calling `mmtk_object_probable_write` directly without a fast-path.
    intptr_t addr = (intptr_t) (void*) new_obj;
    uint8_t* meta_addr = (uint8_t*) (SIDE_METADATA_BASE_ADDRESS + (addr >> 6));
    intptr_t shift = (addr >> 3) & 0b111;
    uint8_t byte_val = *meta_addr;
    if (((byte_val >> shift) & 1) == 1) {
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
    intptr_t addr = (intptr_t) (void*) src;
    uint8_t* meta_addr = (uint8_t*) (SIDE_METADATA_BASE_ADDRESS + (addr >> 6));
    intptr_t shift = (addr >> 3) & 0b111;
    uint8_t byte_val = *meta_addr;
    if (((byte_val >> shift) & 1) == 1) {
      // MMTkObjectBarrierSetRuntime::object_reference_write_pre_slow()((void*) src);
      object_reference_write_slow_call((void*) src, (void*) slot, (void*) target);
    }
  } else {
    object_reference_write_post_call((void*) src, (void*) slot, (void*) target);
  }
}

#ifdef COMPILER1
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
    __ branch(lir_cond_equal, slow);
  } else {
    __ jump(slow);
  }

  __ branch_destination(slow->continuation());
}

#undef __
#endif

#ifdef COMPILER2
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
#endif
