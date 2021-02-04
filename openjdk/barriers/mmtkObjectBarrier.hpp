#ifndef MMTK_BARRIERS_OBJECT_BARRIER
#define MMTK_BARRIERS_OBJECT_BARRIER

#include "opto/callnode.hpp"
#include "opto/idealKit.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "../mmtkBarrierSet.hpp"
#include "../mmtkBarrierSetAssembler_x86.hpp"
#include "../mmtkBarrierSetC1.hpp"
#include "../mmtkBarrierSetC2.hpp"

class MMTkObjectBarrierRuntime: public MMTkBarrierRuntime {
public:
  static void record_modified_node_slow(void* src);

  virtual bool is_slow_path_call(address call) {
    return call == CAST_FROM_FN_PTR(address, record_modified_node_slow);
  }

  virtual void record_modified_node(oop src) {
    record_modified_node_slow((void*) src);
  }
};

class MMTkObjectBarrierC1;
class MMTkObjectBarrierStub;

class MMTkObjectBarrierAssembler: public MMTkBarrierAssembler {
#define __ masm->
  void oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Address dst, Register val, Register tmp1, Register tmp2) {
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

  void record_modified_node(MacroAssembler* masm, Register obj) {
    __ pusha();
    __ movptr(c_rarg0, obj);
    __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkObjectBarrierRuntime::record_modified_node_slow), 1);
    __ popa();
  }
#undef __
public:
  virtual void store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Address dst, Register val, Register tmp1, Register tmp2) {
    if (type == T_OBJECT || type == T_ARRAY) {
      oop_store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    } else {
      BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    }
  }
  void gen_write_barrier_stub(LIR_Assembler* ce, MMTkObjectBarrierStub* stub);
#define __ sasm->
  void generate_c1_write_barrier_runtime_stub(StubAssembler* sasm) {
    __ prologue("mmtk_write_barrier", false);

    Address store_addr(rbp, 3*BytesPerWord);

    Label done;
    Label runtime;

    __ push(c_rarg0);
    // __ push(c_rarg1);
    // __ push(c_rarg2);
    __ push(rax);

    __ load_parameter(0, c_rarg0);
    // __ load_parameter(1, c_rarg1);
    // __ load_parameter(2, c_rarg2);

    __ bind(runtime);

    __ save_live_registers_no_oop_map(true);

    __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkObjectBarrierRuntime::record_modified_node_slow), 1);

    __ restore_live_registers(true);

    __ bind(done);
    __ pop(rax);
    // __ pop(c_rarg2);
    // __ pop(c_rarg1);
    __ pop(c_rarg0);

    __ epilogue();
  }
#undef __
};

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

struct MMTkObjectBarrierStub: CodeStub {
  LIR_Opr _src, _slot, _new_val;
  MMTkObjectBarrierStub(LIR_Opr src, LIR_Opr slot, LIR_Opr new_val): _src(src), _slot(slot), _new_val(new_val) {}
  virtual void emit_code(LIR_Assembler* ce) {
    MMTkObjectBarrierAssembler* bs = (MMTkObjectBarrierAssembler*) ((MMTkBarrierSet*) BarrierSet::barrier_set())->_assembler;
    bs->gen_write_barrier_stub(ce, this);
  }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case();
    if (_src != NULL) visitor->do_input(_src);
    if (_slot != NULL) visitor->do_input(_slot);
    if (_new_val != NULL) visitor->do_input(_new_val);
  }
  NOT_PRODUCT(virtual void print_name(outputStream* out) const { out->print("MMTkWriteBarrierStub"); });
};

class MMTkObjectBarrierC1: public MMTkBarrierC1 {
public:
  class MMTkObjectBarrierCodeGenClosure : public StubAssemblerCodeGenClosure {
    virtual OopMapSet* generate_code(StubAssembler* sasm) {
      MMTkObjectBarrierAssembler* bs = (MMTkObjectBarrierAssembler*) ((MMTkBarrierSet*) BarrierSet::barrier_set())->_assembler;
      bs->generate_c1_write_barrier_runtime_stub(sasm);
      return NULL;
    }
  };

  void write_barrier(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) {
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

public:
  CodeBlob* _write_barrier_c1_runtime_code_blob;
  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value) {
    BarrierSetC1::store_at_resolved(access, value);
    if (access.is_oop()) write_barrier(access, access.base().opr(), access.resolved_addr(), value);
  }
  virtual LIR_Opr atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
    LIR_Opr result = BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
    if (access.is_oop()) write_barrier(access, access.base().opr(), access.resolved_addr(), new_value.result());
    return result;
  }
  virtual LIR_Opr atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) {
    LIR_Opr result = BarrierSetC1::atomic_xchg_at_resolved(access, value);
    if (access.is_oop()) write_barrier(access, access.base().opr(), access.resolved_addr(), value.result());
    return result;
  }
  virtual void generate_c1_runtime_stubs(BufferBlob* buffer_blob) {
    MMTkObjectBarrierCodeGenClosure write_code_gen_cl;
    _write_barrier_c1_runtime_code_blob = Runtime1::generate_blob(buffer_blob, -1, "write_code_gen_cl", false, &write_code_gen_cl);
  }
  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register) {
    DecoratorSet decorators = access.decorators();
    bool needs_patching = (decorators & C1_NEEDS_PATCHING) != 0;
    bool is_write = (decorators & C1_WRITE_ACCESS) != 0;
    bool is_array = (decorators & IS_ARRAY) != 0;
    bool on_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
    bool precise = is_array || on_anonymous;
    resolve_in_register |= !needs_patching && is_write && access.is_oop() && precise;
    return BarrierSetC1::resolve_address(access, resolve_in_register);
  }
};

#undef __

#define __ ideal.

class MMTkObjectBarrierC2: public MMTkBarrierC2 {
  const TypeFunc* record_modified_node_entry_Type() const {
    const Type **fields = TypeTuple::fields(1);
    fields[TypeFunc::Parms+0] = TypeOopPtr::BOTTOM; // oop src
    const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+1, fields);
    fields = TypeTuple::fields(0);
    const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+0, fields);
    return TypeFunc::make(domain, range);
  }
  void record_modified_node(GraphKit* kit, Node* node) const {
    IdealKit ideal(kit, true);
    const TypeFunc *tf = record_modified_node_entry_Type();
    Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkObjectBarrierRuntime::record_modified_node_slow), "record_modified_node", node);
    kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
  }
public:
  virtual Node* store_at_resolved(C2Access& access, C2AccessValue& val) const {
    Node* store = BarrierSetC2::store_at_resolved(access, val);
    if (access.is_oop()) record_modified_node(access.kit(), access.base());
    return store;
  }
  virtual Node* atomic_cmpxchg_val_at_resolved(C2AtomicAccess& access, Node* expected_val, Node* new_val, const Type* value_type) const {
    Node* result = BarrierSetC2::atomic_cmpxchg_val_at_resolved(access, expected_val, new_val, value_type);
    if (access.is_oop()) record_modified_node(access.kit(), access.base());
    return result;
  }
  virtual Node* atomic_cmpxchg_bool_at_resolved(C2AtomicAccess& access, Node* expected_val, Node* new_val, const Type* value_type) const {
    Node* load_store = BarrierSetC2::atomic_cmpxchg_bool_at_resolved(access, expected_val, new_val, value_type);
    if (access.is_oop()) record_modified_node(access.kit(), access.base());
    return load_store;
  }
  virtual Node* atomic_xchg_at_resolved(C2AtomicAccess& access, Node* new_val, const Type* value_type) const {
    Node* result = BarrierSetC2::atomic_xchg_at_resolved(access, new_val, value_type);
    if (access.is_oop()) record_modified_node(access.kit(), access.base());
    return result;
  }
  virtual void clone(GraphKit* kit, Node* src, Node* dst, Node* size, bool is_array) const {
    BarrierSetC2::clone(kit, src, dst, size, is_array);
    record_modified_node(kit, dst);
  }
  virtual bool is_gc_barrier_node(Node* node) const {
    if (node->Opcode() != Op_CallLeaf) return false;
    CallLeafNode *call = node->as_CallLeaf();
    return call->_name != NULL && strcmp(call->_name, "record_modified_node") == 0;
  }
};

#undef __

#endif