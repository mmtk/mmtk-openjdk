#ifndef MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP

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

class MMTkObjectBarrierSetRuntime: public MMTkUnlogBitBarrierSetRuntime {
public:
  // Interfaces called by `MMTkBarrierSet::AccessBarrier`
  virtual void object_reference_write_post(oop src, oop* slot, oop target) const override;
  virtual void object_reference_array_copy_post(oop* src, oop* dst, size_t count) const override {
    object_reference_array_copy_post_call((void*) src, (void*) dst, count);
  }
  virtual void object_probable_write(oop new_obj) const override;
};

class MMTkObjectBarrierSetAssembler: public MMTkUnlogBitBarrierSetAssembler {
protected:
  virtual void object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, bool compensate_val_reg) const override;
public:
  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) override;
  virtual void arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) override;
};

class MMTkObjectBarrierSetC1: public MMTkUnlogBitBarrierSetC1 {
protected:
  virtual void object_reference_write_post(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const override;

  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register) override {
    return MMTkBarrierSetC1::resolve_address_in_register(access, resolve_in_register);
  }
};

class MMTkObjectBarrierSetC2: public MMTkUnlogBitBarrierSetC2 {
protected:
  virtual void object_reference_write_post(GraphKit* kit, Node* src, Node* slot, Node* val) const override;
};

struct MMTkObjectBarrier: MMTkBarrierImpl<
  MMTkObjectBarrierSetRuntime,
  MMTkObjectBarrierSetAssembler,
  MMTkObjectBarrierSetC1,
  MMTkObjectBarrierSetC2
> {};

#endif // MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP
