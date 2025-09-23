#ifndef MMTK_OPENJDK_BARRIERS_MMTK_SATB_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_SATB_BARRIER_HPP

#include "../mmtk.h"
#include "../mmtkBarrierSet.hpp"
#include "../mmtkBarrierSetAssembler_x86.hpp"
#include "../mmtkBarrierSetC1.hpp"
#include "../mmtkBarrierSetC2.hpp"
#include "mmtkUnlogBitBarrier.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "gc/shared/barrierSet.hpp"
#include "opto/callnode.hpp"
#include "opto/idealKit.hpp"

// Debug flag for enabling/disabling all weak reference loading barriers.
// Note that the progran can't run correctly without weak reference load barrier,
// unless we disable reference process, too,
// in which case all references are considered strong.
#define WEAK_REFERENCE_LOAD_BARRIER true

class MMTkSATBBarrierSetRuntime: public MMTkUnlogBitBarrierSetRuntime {
public:
  // Interfaces called by `MMTkBarrierSet::AccessBarrier`
  virtual void object_reference_write_pre(oop src, oop* slot, oop target) const override;
  virtual void object_reference_array_copy_pre(oop* src, oop* dst, size_t count) const override {
    if (count == 0) return;
    ::mmtk_array_copy_pre((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) src, (void*) dst, count);
  }
  virtual void object_probable_write(oop new_obj) const override;
  virtual void load_reference(DecoratorSet decorators, oop value) const override;
};

class MMTkSATBBarrierSetAssembler: public MMTkUnlogBitBarrierSetAssembler {
protected:
  virtual void object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const override;
public:
  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) override;
  virtual void load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp_thread) override;
};

class MMTkSATBBarrierSetC1: public MMTkUnlogBitBarrierSetC1 {
protected:
  virtual void object_reference_write_pre(LIRAccess& access) const override;

  virtual void load_at_resolved(LIRAccess& access, LIR_Opr result) override;

  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register) override {
    return MMTkBarrierSetC1::resolve_address_in_register(access, resolve_in_register);
  }
};

class MMTkSATBBarrierSetC2: public MMTkUnlogBitBarrierSetC2 {
protected:
  virtual void object_reference_write_pre(GraphKit* kit, Node* src, Node* slot, Node* val) const override;

public:
  virtual bool array_copy_requires_gc_barriers(BasicType type) const override {
    return false;
  }
  virtual Node* load_at_resolved(C2Access& access, const Type* val_type) const override;
};

struct MMTkSATBBarrier: MMTkBarrierImpl<
  MMTkSATBBarrierSetRuntime,
  MMTkSATBBarrierSetAssembler,
  MMTkSATBBarrierSetC1,
  MMTkSATBBarrierSetC2
> {};

#endif // MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP
