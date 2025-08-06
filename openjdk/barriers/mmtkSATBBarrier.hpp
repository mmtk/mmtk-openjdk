#ifndef MMTK_OPENJDK_BARRIERS_MMTK_SATB_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_SATB_BARRIER_HPP

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

#define SIDE_METADATA_WORST_CASE_RATIO_LOG 1
#define LOG_BYTES_IN_CHUNK 22
#define CHUNK_MASK ((1L << LOG_BYTES_IN_CHUNK) - 1)

const intptr_t SATB_METADATA_BASE_ADDRESS = (intptr_t) GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS;

class MMTkSATBBarrierSetRuntime: public MMTkBarrierSetRuntime {
public:
  // Interfaces called by `MMTkBarrierSet::AccessBarrier`
  virtual void object_reference_write_pre(oop src, oop* slot, oop target) const override;
  virtual void object_reference_array_copy_pre(oop* src, oop* dst, size_t count) const override {
    if (count == 0) return;
    ::mmtk_array_copy_pre((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) src, (void*) dst, count);
  }
  virtual void object_probable_write(oop new_obj) const override;
  virtual void load_reference(DecoratorSet decorators, oop value) const override;
  virtual void clone_pre(DecoratorSet decorators, oop value) const override {
  };
};

class MMTkSATBBarrierSetAssembler: public MMTkBarrierSetAssembler {
protected:
  virtual void object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const override;
  /// Generate C1 write barrier slow-call assembly code
  virtual void generate_c1_pre_write_barrier_runtime_stub(StubAssembler* sasm) const;
public:
  virtual void generate_c1_pre_write_barrier_stub(LIR_Assembler* ce, MMTkC1PreBarrierStub* stub) const;
  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) override;
  virtual void load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp_thread) override;
};

class MMTkSATBBarrierSetC1: public MMTkBarrierSetC1 {
protected:
  virtual void object_reference_write_pre(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val, CodeEmitInfo* info) const override;

  virtual void load_at_resolved(LIRAccess& access, LIR_Opr result) override;

  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register) override {
    return MMTkBarrierSetC1::resolve_address_in_register(access, resolve_in_register);
  }
};

class MMTkSATBBarrierSetC2: public MMTkBarrierSetC2 {
protected:
  virtual void object_reference_write_pre(GraphKit* kit, Node* src, Node* slot, Node* pre_val, Node* val) const override;

public:
  virtual bool array_copy_requires_gc_barriers(BasicType type) const override {
    return false;
  }
  virtual Node* load_at_resolved(C2Access& access, const Type* val_type) const override;
  virtual void clone(GraphKit* kit, Node* src, Node* dst, Node* size, bool is_array) const override;

  virtual Node* atomic_xchg_at_resolved(C2AtomicAccess& access, Node* new_val, const Type* value_type) const {
    Node* result = BarrierSetC2::atomic_xchg_at_resolved(access, new_val, value_type);
    if (access.is_oop()) {
      object_reference_write_pre(access.kit(), access.base(), access.addr().node(), result, new_val);
      object_reference_write_post(access.kit(), access.base(), access.addr().node(), new_val);
    }
    return result;
  }

};

struct MMTkSATBBarrier: MMTkBarrierImpl<
  MMTkSATBBarrierSetRuntime,
  MMTkSATBBarrierSetAssembler,
  MMTkSATBBarrierSetC1,
  MMTkSATBBarrierSetC2
> {};

#endif // MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP
