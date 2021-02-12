#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

void MMTkObjectBarrierSetRuntime::record_modified_node_slow(void* obj) {
  ::record_modified_node((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) obj);
}

void MMTkObjectBarrierSetRuntime::record_modified_node(oop src) {
  record_modified_node_slow((void*) src);
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

  assert_different_registers(tmp1, noreg);
  assert_different_registers(tmp2, noreg);
  assert_different_registers(tmp1, tmp2);

  __ push(dst.base());

  if (dst.index() == noreg && dst.disp() == 0) {
    if (dst.base() != tmp1) {
      __ movptr(tmp1, dst.base());
    }
  } else {
    __ lea(tmp1, dst);
  }

  BarrierSetAssembler::store_at(masm, decorators, type, Address(tmp1, 0), val, noreg, noreg);

  __ pop(tmp2);

  record_modified_node(masm, tmp2);
}

void MMTkObjectBarrierSetAssembler::record_modified_node(MacroAssembler* masm, Register obj) {
  __ pusha();
  __ movptr(c_rarg0, obj);
  __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_modified_node_slow), 1);
  __ popa();
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
  __ jump(slow);
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

void MMTkObjectBarrierSetC2::record_modified_node(GraphKit* kit, Node* node) const {
  IdealKit ideal(kit, true);
  const TypeFunc *tf = record_modified_node_entry_Type();
  Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::record_modified_node_slow), "record_modified_node", node);
  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

#undef __