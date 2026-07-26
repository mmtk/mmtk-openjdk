#ifndef MMTK_OPENJDK_BARRIERS_MMTK_UNLOG_BIT_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_UNLOG_BIT_BARRIER_HPP

#include "../mmtkBarrierSet.hpp"
#include "../mmtkBarrierSetC1.hpp"
#include "../mmtkBarrierSetC2.hpp"

/// This file contains abstract barrier sets for barriers based on the (object-grained) unlog bit.

struct MMTkC1UnlogBitBarrierSlowPathStub;

inline uintptr_t unlog_bit_base_address() {
  return get_global_side_metadata_vm_base_address();
}

//////////////////// Runtime ////////////////////

class MMTkUnlogBitBarrierSetRuntime: public MMTkBarrierSetRuntime {
protected:
  static bool is_unlog_bit_set(oop obj) {
    uintptr_t addr = (uintptr_t) (void*) obj;
    uint8_t* meta_addr = (uint8_t*) (unlog_bit_base_address() + (addr >> 6));
    uintptr_t shift = (addr >> 3) & 0b111;
    uint8_t byte_val = *meta_addr;
    return ((byte_val >> shift) & 1) == 1;
  }
};

//////////////////// C1 ////////////////////

class MMTkUnlogBitBarrierSetC1: public MMTkBarrierSetC1 {
protected:
  static void emit_check_unlog_bit_fast_path(LIRGenerator* gen, LIR_Opr addr, CodeStub* slow);
  static void object_reference_write_pre_or_post(LIRAccess& access, LIR_Opr src, bool pre);
};

/// C1 write barrier slow path stub.
///
/// This stub calls `MMTkBarrierSetRuntime::object_reference_write_{slow,pre,post}_call` depending
/// on whether barrier fast paths are enabled and whether it is pre or post barrier, passing the
/// `src` argument, and leaving other arguments as nullptr.  This is enough for object-remembering
/// barriers based on the unlog bit, including the ObjectBarrier and the SATBBarrier, because only
/// the `src` argument is significant.
///
/// Note that this stub cannot be generalized to field-remembering barriers as it does not pass the
/// field or the old/new values.  Field-remembering barriers should implement their own slow-path
/// stub(s).
struct MMTkC1UnlogBitBarrierSlowPathStub: CodeStub {
  LIR_Opr src;
  bool fast_path_enabled;
  bool pre;

  MMTkC1UnlogBitBarrierSlowPathStub(LIR_Opr src, bool fast_path_enabled, bool pre):
      src(src), fast_path_enabled(fast_path_enabled), pre(pre) {
    FrameMap* f = Compilation::current()->frame_map();
    f->update_reserved_argument_area_size(3 * BytesPerWord);
  }

  virtual void emit_code(LIR_Assembler* ce) override;

  virtual void visit(LIR_OpVisitState* visitor) override {
    visitor->do_slow_case();
    assert(src->is_valid(), "src must be valid");
    visitor->do_input(src);
  }

  NOT_PRODUCT(virtual void print_name(outputStream* out) const override { out->print("MMTkC1UnlogBitBarrierSlowPathStub"); });
};

//////////////////// C2 ////////////////////

class MMTkUnlogBitBarrierSetC2: public MMTkBarrierSetC2 {
protected:
  static Node* emit_check_unlog_bit_fast_path(MMTkIdealKit& ideal, Node* obj);
  static void object_reference_write_pre_or_post(MMTkIdealKit& ideal, Node* src, bool pre);
};

#endif // MMTK_OPENJDK_BARRIERS_MMTK_UNLOG_BIT_BARRIER_HPP
