#ifndef MMTK_OPENJDK_BARRIERS_MMTK_UNLOG_BIT_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_UNLOG_BIT_BARRIER_HPP

#include "../mmtkBarrierSet.hpp"
#include "../mmtkBarrierSetAssembler_x86.hpp"
#include "../mmtkBarrierSetC1.hpp"
#include "../mmtkBarrierSetC2.hpp"

/// This file contains abstract barrier sets for barriers based on the (object-grained) unlog bit.

const uintptr_t UNLOG_BIT_BASE_ADDRESS = GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS;

class MMTkUnlogBitBarrierSetRuntime: public MMTkBarrierSetRuntime {
protected:
  static bool is_unlog_bit_set(oop obj) {
    uintptr_t addr = (uintptr_t) (void*) obj;
    uint8_t* meta_addr = (uint8_t*) (UNLOG_BIT_BASE_ADDRESS + (addr >> 6));
    uintptr_t shift = (addr >> 3) & 0b111;
    uint8_t byte_val = *meta_addr;
    return ((byte_val >> shift) & 1) == 1;
  }
};

class MMTkUnlogBitBarrierSetAssembler: public MMTkBarrierSetAssembler {
protected:
  static void emit_check_unlog_bit_fast_path(MacroAssembler* masm, Label &done, Register obj, Register tmp1, Register tmp2);
  static void object_reference_write_pre_or_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, bool pre);
};

class MMTkUnlogBitBarrierSetC1: public MMTkBarrierSetC1 {
protected:
  static void emit_check_unlog_bit_fast_path(LIRGenerator* gen, LIR_Opr addr, CodeStub* slow);
};

class MMTkUnlogBitBarrierSetC2: public MMTkBarrierSetC2 {};

#endif // MMTK_OPENJDK_BARRIERS_MMTK_UNLOG_BIT_BARRIER_HPP
