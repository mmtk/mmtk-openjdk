#ifndef MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP

#include "../mmtk.h"
#include "../mmtkBarrierSet.hpp"
#include "utilities/macros.hpp"
#include CPU_HEADER(mmtkBarrierSetAssembler)
#include CPU_HEADER(mmtkObjectBarrierSetAssembler)
#include "mmtkUnlogBitBarrier.hpp"
#ifdef COMPILER1
#include "../mmtkBarrierSetC1.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#endif
#ifdef COMPILER2
#include "../mmtkBarrierSetC2.hpp"
#include "opto/callnode.hpp"
#include "opto/idealKit.hpp"
#endif
#include "gc/shared/barrierSet.hpp"

/// This file supports the `ObjectBarrier` in MMTk core,
/// i.e. the barrier that remembers the object when it is first modified.
/// Despite the name, `ObjectBarrier` is not the only barrier that uses the object-grained unlogging bit.
/// `SATBBarrier` also uses the object-grained unlog bit.
/// We keep the name in sync with the MMTk core.

//////////////////// Runtime ////////////////////

class MMTkObjectBarrierSetRuntime: public MMTkUnlogBitBarrierSetRuntime {
public:
  // Interfaces called by `MMTkBarrierSet::AccessBarrier`
  virtual void object_reference_write_post(oop src, oop* slot, oop target) const override;
  virtual void object_reference_array_copy_post(oop* src, oop* dst, size_t count) const override {
    object_reference_array_copy_post_call((void*) src, (void*) dst, count);
  }
  virtual void object_probable_write(oop new_obj) const override;
};

//////////////////// C1 ////////////////////

#ifdef COMPILER1
class MMTkObjectBarrierSetC1: public MMTkUnlogBitBarrierSetC1 {
protected:
  virtual void object_reference_write_post(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const override;

  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register) override {
    return MMTkBarrierSetC1::resolve_address_in_register(access, resolve_in_register);
  }
};
#else
class MMTkObjectBarrierSetC1;
#endif

//////////////////// C2 ////////////////////

#ifdef COMPILER2
class MMTkObjectBarrierSetC2: public MMTkUnlogBitBarrierSetC2 {
protected:
  virtual void object_reference_write_post(GraphKit* kit, Node* src, Node* slot, Node* val) const override;
};
#else
class MMTkObjectBarrierSetC2;
#endif

//////////////////// Impl ////////////////////

struct MMTkObjectBarrier: MMTkBarrierImpl<
  MMTkObjectBarrierSetRuntime,
  MMTkObjectBarrierSetAssembler,
  MMTkObjectBarrierSetC1,
  MMTkObjectBarrierSetC2
> {};

#endif // MMTK_OPENJDK_BARRIERS_MMTK_OBJECT_BARRIER_HPP
