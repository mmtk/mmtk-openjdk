#ifndef MMTK_OPENJDK_BARRIERS_MMTK_UNLOG_BIT_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_UNLOG_BIT_BARRIER_HPP

#include "../mmtkBarrierSet.hpp"
#include "../mmtkBarrierSetAssembler_x86.hpp"
#include "../mmtkBarrierSetC1.hpp"
#include "../mmtkBarrierSetC2.hpp"

/// This file contains abstract barrier sets for barriers based on the (object-grained) unlog bit.

struct MMTkC1UnlogBitBarrierSlowPathStub;

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

public:
  /// Generate C1 barrier slow path stub
  void generate_c1_unlog_bit_barrier_slow_path_stub(LIR_Assembler* ce, MMTkC1UnlogBitBarrierSlowPathStub* stub) const;
};

class MMTkUnlogBitBarrierSetC1: public MMTkBarrierSetC1 {
protected:
  static void emit_check_unlog_bit_fast_path(LIRGenerator* gen, LIR_Opr addr, CodeStub* slow);
  static void object_reference_write_pre_or_post(LIRAccess& access, bool pre);
};

/// C1 pre write barrier slow-call stub.
/// The default behaviour is to call `MMTkBarrierSetRuntime::object_reference_write_pre_call` and pass all the three args.
/// Barrier implementations may inherit from this class, and override `emit_code` to perform a specialized slow-path call.
struct MMTkC1UnlogBitBarrierSlowPathStub: CodeStub {
  LIR_Opr src;
  bool pre;

  MMTkC1UnlogBitBarrierSlowPathStub(LIR_Opr src, bool pre): src(src), pre(pre) {}

  virtual void emit_code(LIR_Assembler* ce) override;

  virtual void visit(LIR_OpVisitState* visitor) override {
    visitor->do_slow_case();
    assert(src->is_valid(), "src must be valid");
    visitor->do_input(src);
  }

  NOT_PRODUCT(virtual void print_name(outputStream* out) const { out->print("MMTkC1PreBarrierStub"); });
};

class MMTkUnlogBitBarrierSetC2: public MMTkBarrierSetC2 {};

#endif // MMTK_OPENJDK_BARRIERS_MMTK_UNLOG_BIT_BARRIER_HPP
