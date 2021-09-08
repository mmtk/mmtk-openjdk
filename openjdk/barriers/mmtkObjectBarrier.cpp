#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

void MMTkObjectBarrierSetRuntime::record_modified_node_slow(void* src, void* slot, void* val) {
  ::mmtk_object_reference_write((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, src, slot, val);
}

void MMTkObjectBarrierSetRuntime::record_clone_slow(void* src, void* dst, size_t size) {
  ::mmtk_object_reference_clone((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, src, dst, size);
}

void MMTkObjectBarrierSetRuntime::record_modified_node(oop src, ptrdiff_t offset, oop val) {
#if MMTK_ENABLE_BARRIER_FASTPATH
    intptr_t addr = (intptr_t) (void*) src;
    uint8_t* meta_addr = (uint8_t*) (SIDE_METADATA_BASE_ADDRESS + (addr >> 6));
    intptr_t shift = (addr >> 3) & 0b111;
    uint8_t byte_val = *meta_addr;
    if (((byte_val >> shift) & 1) == 1) {
      record_modified_node_slow((void*) src, (void*) (((intptr_t) (void*) src) + offset), (void*) val);
    }
#else
    record_modified_node_slow((void*) src, (void*) (((intptr_t) (void*) src) + offset), (void*) val);
#endif
}

void MMTkObjectBarrierSetRuntime::record_clone(oop src, oop dst, size_t size) {
#if MMTK_ENABLE_BARRIER_FASTPATH
  intptr_t addr = (intptr_t) (void*) dst;
  uint8_t* meta_addr = (uint8_t*) (SIDE_METADATA_BASE_ADDRESS + (addr >> 6));
  intptr_t shift = (addr >> 3) & 0b111;
  uint8_t byte_val = *meta_addr;
  if (((byte_val >> shift) & 1) == 1) {
    record_clone_slow((void*) src, (void*) dst, size);
  }
#else
  record_clone_slow((void*) src, (void*) dst, size);
#endif
}

void MMTkObjectBarrierSetRuntime::record_arraycopy(arrayOop src_obj, size_t src_offset_in_bytes, oop* src_raw, arrayOop dst_obj, size_t dst_offset_in_bytes, oop* dst_raw, size_t length) {
#if MMTK_ENABLE_BARRIER_FASTPATH
  intptr_t addr = (intptr_t) (void*) dst_obj;
  uint8_t* meta_addr = (uint8_t*) (SIDE_METADATA_BASE_ADDRESS + (addr >> 6));
  intptr_t shift = (addr >> 3) & 0b111;
  uint8_t byte_val = *meta_addr;
  if (((byte_val >> shift) & 1) == 1) {
    ::mmtk_object_reference_arraycopy((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) src_obj, src_offset_in_bytes, (void*) dst_obj, dst_offset_in_bytes, length);
  }
#else
  ::mmtk_object_reference_arraycopy((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) src_obj, src_offset_in_bytes, (void*) dst_obj, dst_offset_in_bytes, length);
#endif
}

#define __ masm->

void MMTkObjectBarrierSetAssembler::oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Address dst, Register val, Register tmp1, Register tmp2) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool as_normal = (decorators & AS_NORMAL) != 0;
  assert((decorators & IS_DEST_UNINITIALIZED) == 0, "unsupported");

  if (!in_heap || val == noreg) {
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    return;
  }

  record_modified_node(masm, dst, val, tmp1, tmp2);

  BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
}

void MMTkObjectBarrierSetAssembler::record_modified_node(MacroAssembler* masm, Address dst, Register val, Register tmp1, Register tmp2) {
#if MMTK_ENABLE_BARRIER_FASTPATH
  Label done;

  Register tmp3 = rscratch1;
  Register tmp4 = rscratch2;
  Register tmp5 = tmp1 == dst.base() || tmp1 == dst.index() ? tmp2 : tmp1;

  // tmp5 = load-byte (SIDE_METADATA_BASE_ADDRESS + (obj >> 6));
  __ movptr(tmp3, dst.base());
  __ shrptr(tmp3, 6);
  __ movptr(tmp5, SIDE_METADATA_BASE_ADDRESS);
  __ movb(tmp5, Address(tmp5, tmp3));
  // tmp3 = (obj >> 3) & 7
  __ movptr(tmp3, dst.base());
  __ shrptr(tmp3, 3);
  __ andptr(tmp3, 7);
  // tmp5 = tmp5 >> tmp3
  __ movptr(tmp4, rcx);
  __ movl(rcx, tmp3);
  __ shrptr(tmp5);
  __ movptr(rcx, tmp4);
  // if ((tmp5 & 1) == 0) goto slowpath;
  __ andptr(tmp5, 1);
  __ cmpptr(tmp5, 1);
  __ jcc(Assembler::notEqual, done);

  __ pusha();
  __ movptr(c_rarg0, dst.base());
  __ lea(c_rarg1, dst);
  if (val == noreg) {
    __ movptr(c_rarg2, (int32_t) NULL_WORD);
  } else {
    __ movptr(c_rarg2, val);
  }
  __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_modified_node_slow), 3);
  __ popa();

  __ bind(done);
#else
  __ pusha();
  __ movptr(c_rarg0, dst.base());
  __ lea(c_rarg1, dst);
  if (val == noreg) {
    __ movptr(c_rarg2, (int32_t) NULL_WORD);
  } else {
    __ movptr(c_rarg2, val);
  }
  __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_modified_node_slow), 3);
  __ popa();
#endif
}

#undef __

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkObjectBarrierSetC1::record_modified_node(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) {
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
    // LIR_Opr reg = gen->new_pointer_register();
    // if (slot->is_constant()) {
    //   __ move(slot, reg);
    // } else {
    //   __ leal(slot, reg);
    // }
    // slot = reg;

    LIR_Address* address = slot->as_address_ptr();
    LIR_Opr ptr = gen->new_pointer_register();
    if (!address->index()->is_valid() && address->disp() == 0) {
      __ move(address->base(), ptr);
    } else {
      assert(address->disp() != max_jint, "lea doesn't support patched addresses!");
      __ leal(slot, ptr);
    }
    slot = ptr;
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
  CodeStub* slow = new MMTkObjectBarrierStub(src, slot, new_val);

#if MMTK_ENABLE_BARRIER_FASTPATH
  LIR_Opr addr = src;
  // uint8_t* meta_addr = (uint8_t*) (SIDE_METADATA_BASE_ADDRESS + (addr >> 6));
  LIR_Opr offset = gen->new_pointer_register();
  __ move(addr, offset);
  __ shift_right(offset, 6, offset);
  LIR_Opr base = gen->new_pointer_register();
  __ move(LIR_OprFact::longConst(SIDE_METADATA_BASE_ADDRESS), base);
  LIR_Address* meta_addr = new LIR_Address(base, offset, T_BYTE);
  // intptr_t shift = (addr >> 3) & 0b111;
  LIR_Opr shift_long = gen->new_pointer_register();
  __ move(addr, shift_long);
  __ shift_right(shift_long, 3, shift_long);
  __ logical_and(shift_long, LIR_OprFact::longConst(0b111), shift_long);
  LIR_Opr shift_int = gen->new_register(T_INT);
  __ convert(Bytecodes::_l2i, shift_long, shift_int);
  LIR_Opr shift = LIRGenerator::shiftCountOpr();
  __ move(shift_int, shift);
  // uint8_t byte_val = *meta_addr;
  LIR_Opr byte_val = gen->new_register(T_INT);
  __ move(meta_addr, byte_val);
  // if (((byte_val >> shift) & 1) == 1) slow;
  LIR_Opr result = byte_val;
  __ shift_right(result, shift, result, LIR_OprFact::illegalOpr);
  __ logical_and(result, LIR_OprFact::intConst(1), result);
  __ cmp(lir_cond_equal, result, LIR_OprFact::intConst(1));
  __ branch(lir_cond_equal, LP64_ONLY(T_LONG) NOT_LP64(T_INT), slow);
#else
  __ jump(slow);
#endif

  __ branch_destination(slow->continuation());
}

#undef __

#define __ ideal.

void MMTkObjectBarrierSetC2::record_modified_node(GraphKit* kit, Node* src, Node* slot, Node* val) const {

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
    Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_modified_node_slow), "record_modified_node", src, slot, val);
  } __ end_if();
#else
  const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM);
  Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_modified_node_slow), "record_modified_node", src, slot, val);
#endif

  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

void MMTkObjectBarrierSetC2::record_clone(GraphKit* kit, Node* src, Node* dst, Node* size) const {
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
    const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeInt::INT);
    Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_clone_slow), "record_clone", src, dst, size);
  } __ end_if();
#else
  const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeInt::INT);
  Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_clone_slow), "record_clone", src, dst, size);
#endif

  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

#undef __