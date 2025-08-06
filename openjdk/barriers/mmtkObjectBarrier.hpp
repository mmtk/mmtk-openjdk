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

#define SIDE_METADATA_WORST_CASE_RATIO_LOG 1
#define LOG_BYTES_IN_CHUNK 22
#define CHUNK_MASK ((1L << LOG_BYTES_IN_CHUNK) - 1)

const intptr_t SIDE_METADATA_BASE_ADDRESS = (intptr_t) GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS;

class MMTkObjectBarrierSetRuntime: public MMTkBarrierSetRuntime {
public:
  // Interfaces called by `MMTkBarrierSet::AccessBarrier`
  template<DecoratorSet decorators>
  void object_reference_write_post(oop src, oop* slot, oop target) const {
#if MMTK_ENABLE_BARRIER_FASTPATH
    intptr_t addr = (intptr_t) (void*) src;
    uint8_t* meta_addr = (uint8_t*) (SIDE_METADATA_BASE_ADDRESS + (addr >> 6));
    intptr_t shift = (addr >> 3) & 0b111;
    uint8_t byte_val = *meta_addr;
    if (((byte_val >> shift) & 1) == 1) {
      // MMTkObjectBarrierSetRuntime::object_reference_write_pre_slow()((void*) src);
      object_reference_write_slow_call((void*) src, (void*) slot, (void*) target);
    }
#else
    object_reference_write_post_call((void*) src, (void*) slot, (void*) target);
#endif
  }
  virtual void object_reference_array_copy_post(oop* src, oop* dst, size_t count) const override {
    object_reference_array_copy_post_call((void*) src, (void*) dst, count);
  }
  virtual void object_probable_write(oop new_obj) const override;
};

class MMTkObjectBarrierSetAssembler: public MMTkBarrierSetAssembler {
protected:
  virtual void object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, bool compensate_val_reg) const override;
  /// Generate C1 write barrier slow-call assembly code
  virtual void generate_c1_post_write_barrier_runtime_stub(StubAssembler* sasm) const;
public:
  virtual void generate_c1_post_write_barrier_stub(LIR_Assembler* ce, MMTkC1PostBarrierStub* stub) const;
  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) override;
  virtual void arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) override;
};

class MMTkObjectBarrierSetC1: public MMTkBarrierSetC1 {
protected:
  virtual void object_reference_write_post(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const override;

  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register) override {
    return MMTkBarrierSetC1::resolve_address_in_register(access, resolve_in_register);
  }
};

class MMTkObjectBarrierSetC2: public MMTkBarrierSetC2 {
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
