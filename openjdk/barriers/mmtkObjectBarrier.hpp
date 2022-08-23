#ifndef MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP

#include "../mmtk.h"
#include "../mmtkBarrierSet.hpp"
#include "../mmtkBarrierSetAssembler_x86.hpp"
#include "../mmtkBarrierSetC1.hpp"
#include "../mmtkBarrierSetC2.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "gc/shared/barrierSet.hpp"
#include "opto/callnode.hpp"
#include "opto/idealKit.hpp"

#define MMTK_ENABLE_OBJECT_BARRIER_FASTPATH true

#define SIDE_METADATA_WORST_CASE_RATIO_LOG 1
#define LOG_BYTES_IN_CHUNK 22
#define CHUNK_MASK ((1L << LOG_BYTES_IN_CHUNK) - 1)

const intptr_t SIDE_METADATA_BASE_ADDRESS = (intptr_t) GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS;

template<PlanSelector plan>
struct SlowCall {};

template<>
struct SlowCall<PlanSelector::GenCopy> {
  static void object_reference_write_pre_slow(void* src) {
    mmtk_gen_object_barrier_slow((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, src);
  }
};

template<>
struct SlowCall<PlanSelector::GenImmix> {
  static void object_reference_write_pre_slow(void* src) {
    mmtk_gen_object_barrier_slow((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, src);
  }
};


typedef void (*object_reference_write_pre_slow_fn)(void* src);

class MMTkObjectBarrierSetRuntime: public MMTkBarrierSetRuntime {
public:
  static object_reference_write_pre_slow_fn object_reference_write_pre_slow() {
    switch (mmtk_get_active_plan()) {
      case PlanSelector::GenCopy: return SlowCall<PlanSelector::GenCopy>::object_reference_write_pre_slow;
      case PlanSelector::GenImmix: return SlowCall<PlanSelector::GenImmix>::object_reference_write_pre_slow;
      default:
        guarantee(false, "unreachable");
        return NULL;
    }
  }
  static address object_reference_write_pre_slow_address() {
    return CAST_FROM_FN_PTR(address, object_reference_write_pre_slow());
  }

  virtual bool is_slow_path_call(address call) {
    return call == object_reference_write_pre_slow_address()
        || call == CAST_FROM_FN_PTR(address, object_reference_write_pre_)
        || call == CAST_FROM_FN_PTR(address, object_reference_array_copy_pre_);
  }

  static void object_reference_write_pre_(void* src, void* slot, void* target);
  static void object_reference_array_copy_pre_(void* src, void* dst, size_t count);

  virtual void object_reference_write_pre(oop src, oop* slot, oop target) override;
  virtual void object_reference_array_copy_pre(oop* src, oop* dst, size_t count) override;
};

class MMTkObjectBarrierSetC1;
class MMTkObjectBarrierStub;

class MMTkObjectBarrierSetAssembler: public MMTkBarrierSetAssembler {
  void oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Address dst, Register val, Register tmp1, Register tmp2);
  void object_reference_write(MacroAssembler* masm, Address dst, Register val, Register tmp1, Register tmp2);
public:
  virtual void store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Address dst, Register val, Register tmp1, Register tmp2) {
    if (type == T_OBJECT || type == T_ARRAY) {
      oop_store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    } else {
      BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    }
  }
  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) override;
  inline void gen_write_barrier_stub(LIR_Assembler* ce, MMTkObjectBarrierStub* stub);
#define __ sasm->
  void generate_c1_write_barrier_runtime_stub(StubAssembler* sasm) {
    __ prologue("mmtk_write_barrier", false);

    Address store_addr(rbp, 4*BytesPerWord);

    Label done;
    Label runtime;

    __ push(c_rarg0);
    __ push(c_rarg1);
    __ push(c_rarg2);
    __ push(rax);

    __ load_parameter(0, c_rarg0);
    __ load_parameter(1, c_rarg1);
    __ load_parameter(2, c_rarg2);

    __ bind(runtime);

    __ save_live_registers_no_oop_map(true);

    __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkObjectBarrierSetRuntime::object_reference_write_pre_), 3);

    __ restore_live_registers(true);

    __ bind(done);
    __ pop(rax);
    __ pop(c_rarg2);
    __ pop(c_rarg1);
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
    MMTkObjectBarrierSetAssembler* bs = (MMTkObjectBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
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

class MMTkObjectBarrierSetC1: public MMTkBarrierSetC1 {
public:
  class MMTkObjectBarrierCodeGenClosure : public StubAssemblerCodeGenClosure {
    virtual OopMapSet* generate_code(StubAssembler* sasm) {
      MMTkObjectBarrierSetAssembler* bs = (MMTkObjectBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
      bs->generate_c1_write_barrier_runtime_stub(sasm);
      return NULL;
    }
  };
  void object_reference_write_pre(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val);
public:
  CodeBlob* _write_barrier_c1_runtime_code_blob;
  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value) {
    if (access.is_oop()) object_reference_write_pre(access, access.base().opr(), access.resolved_addr(), value);
    BarrierSetC1::store_at_resolved(access, value);
  }
  virtual LIR_Opr atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
    if (access.is_oop()) object_reference_write_pre(access, access.base().opr(), access.resolved_addr(), new_value.result());
    LIR_Opr result = BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
    return result;
  }
  virtual LIR_Opr atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) {
    if (access.is_oop()) object_reference_write_pre(access, access.base().opr(), access.resolved_addr(), value.result());
    LIR_Opr result = BarrierSetC1::atomic_xchg_at_resolved(access, value);
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

class MMTkObjectBarrierSetC2: public MMTkBarrierSetC2 {
protected:
  virtual void object_reference_write_pre(GraphKit* kit, Node* src, Node* slot, Node* val) const override;
};

#undef __

#define __ ce->masm()->
inline void MMTkObjectBarrierSetAssembler::gen_write_barrier_stub(LIR_Assembler* ce, MMTkObjectBarrierStub* stub) {
  MMTkObjectBarrierSetC1* bs = (MMTkObjectBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());
  ce->store_parameter(stub->_src->as_pointer_register(), 0);
  ce->store_parameter(stub->_slot->as_pointer_register(), 1);
  ce->store_parameter(stub->_new_val->as_pointer_register(), 2);
  __ call(RuntimeAddress(bs->_write_barrier_c1_runtime_code_blob->code_begin()));
  __ jmp(*stub->continuation());
}
#undef __

struct MMTkObjectBarrier: MMTkBarrierImpl<
  MMTkObjectBarrierSetRuntime,
  MMTkObjectBarrierSetAssembler,
  MMTkObjectBarrierSetC1,
  MMTkObjectBarrierSetC2
> {};

#endif // MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP
