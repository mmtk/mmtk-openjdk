#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

void MMTkObjectBarrierSetRuntime::record_modified_node_slow(void* obj) {
  ::post_write_barrier_slow((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) obj);
}

void MMTkObjectBarrierSetRuntime::record_modified_node_full(void* obj) {
  ::post_write_barrier((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) obj);
}

void MMTkObjectBarrierSetRuntime::record_modified_node(oop src) {
#if MMTK_ENABLE_OBJECT_BARRIER_FASTPATH
  intptr_t addr = (intptr_t) (void*) src;
  uint8_t* meta_addr = (uint8_t*) (SIDE_METADATA_BASE_ADDRESS + (addr >> 6));
  intptr_t shift = (addr >> 3) & 0b111;
  uint8_t byte_val = *meta_addr;
  if (((byte_val >> shift) & 1) == 1) {
    record_modified_node_slow((void*) src);
  }
#else
  record_modified_node_full((void*) src);
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

  BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);

  record_modified_node(masm, dst.base(), tmp1, tmp2);
}

void MMTkObjectBarrierSetAssembler::record_modified_node(MacroAssembler* masm, Register obj, Register tmp1, Register tmp2) {
#if MMTK_ENABLE_OBJECT_BARRIER_FASTPATH
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

  assert_different_registers(c_rarg0, obj);
  __ movptr(c_rarg0, obj);
  __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_modified_node_slow), 1);

  __ bind(done);
#else
  assert_different_registers(c_rarg0, obj);
  __ movptr(c_rarg0, obj);
  __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_modified_node_full), 1);
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
  CodeStub* slow = new MMTkObjectBarrierStub(src, slot, new_val);

#if MMTK_ENABLE_OBJECT_BARRIER_FASTPATH
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

const TypeFunc* record_modified_node_entry_Type() {
  const Type **fields = TypeTuple::fields(1);
  fields[TypeFunc::Parms+0] = TypeOopPtr::BOTTOM; // oop src
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+1, fields);
  fields = TypeTuple::fields(0);
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+0, fields);
  return TypeFunc::make(domain, range);
}

void MMTkObjectBarrierSetC2::record_modified_node(GraphKit* kit, Node* src, Node* val) const {
  if (val != NULL && val->is_Con()) {
    const Type* t = val->bottom_type();
    if (t == TypePtr::NULL_PTR) return;
  }

  MMTkIdealKit ideal(kit, true);

#if MMTK_ENABLE_OBJECT_BARRIER_FASTPATH
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
    const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM);
    Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_modified_node_slow), "record_modified_node", src);
  } __ end_if();
#else
  const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM);
  Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_modified_node_full), "record_modified_node", src);
#endif

  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

#undef __
