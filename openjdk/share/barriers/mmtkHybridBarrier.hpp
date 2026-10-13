#ifndef MMTK_OPENJDK_BARRIERS_MMTK_HYBRID_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_HYBRID_BARRIER_HPP

#include "../mmtk.h"
#include "../mmtkBarrierSet.hpp"
#include CPU_HEADER(mmtkBarrierSetAssembler)
#include CPU_HEADER(mmtkHybridBarrierSetAssembler)
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

/// This file supports the `HybridBarrier` in MMTk core (used by the `ConcurrentSpan` plan).
///
/// It is a flag-gated pre-write barrier (a hybrid Yuasa/Dijkstra barrier, as in Go).  While the
/// global flag at `mmtk_barrier_active_address()` is non-zero, every reference store calls into
/// MMTk with (src, slot, new value) *before* the store, and MMTk logs both the old value of the slot
/// and the new value.  There is no unlog bit: the check is per store, not per object.
///
/// Fast paths only test the flag byte inline.  Buffer appends are not inlined: every barriered store
/// calls into the runtime while the flag is set.

/// The address of the global flag byte of the hybrid barrier.  Fixed for the lifetime of the VM,
/// so compiled code embeds it as a constant.
inline uint8_t* hybrid_barrier_active_address() {
  static uint8_t* addr = mmtk_barrier_active_address();
  return addr;
}

//////////////////// Runtime ////////////////////

class MMTkHybridBarrierSetRuntime: public MMTkBarrierSetRuntime {
public:
  static bool is_active() {
    return *(volatile uint8_t*) hybrid_barrier_active_address() != 0;
  }

  /// Shade every reference field of `src`, which is about to be cloned.  Called from C2-compiled
  /// code (after testing the flag) and from the runtime.
  static void object_clone_pre_call(void* src);
  /// Array-copy pre-barrier for an uninitialised destination: shade the source values only.
  static void object_reference_array_copy_pre_src_only_call(void* src, void* dst, size_t count);

  // Interfaces called by `MMTkBarrierSet::AccessBarrier`
  virtual void object_reference_write_pre(oop src, oop* slot, oop target) const override;
  virtual void object_reference_array_copy_pre(oop* src, oop* dst, size_t count) const override;
  virtual void object_reference_array_copy_pre_dst_uninitialized(oop* src, oop* dst, size_t count) const override;
  virtual void object_clone_pre(oop src, oop dst) const override;
  virtual void object_probable_write(oop new_obj) const override;
  virtual void object_reference_write_not_in_heap(oop value) const override;
  virtual void load_reference(DecoratorSet decorators, oop value) const override;
};

//////////////////// C1 ////////////////////

#ifdef COMPILER1
/// C1 write barrier slow path stub.  Entered when the flag is set (or always, if barrier fast paths
/// are disabled).  Passes (src, slot, new_val) to the slow path.  If the field offset is not known
/// yet (`patching`), it calls `object_probable_write` on `src` instead, which shades the old values
/// of all fields of `src` and rescans `src` later.
struct MMTkC1HybridBarrierStub: CodeStub {
  LIR_Opr src, slot, new_val;
  bool patching;

  MMTkC1HybridBarrierStub(LIR_Opr src, LIR_Opr slot, LIR_Opr new_val, bool patching):
      src(src), slot(slot), new_val(new_val), patching(patching) {
    FrameMap* f = Compilation::current()->frame_map();
    f->update_reserved_argument_area_size(3 * BytesPerWord);
  }

  virtual void emit_code(LIR_Assembler* ce) override;

  virtual void visit(LIR_OpVisitState* visitor) override {
    visitor->do_slow_case();
    visitor->do_input(src);
    if (!patching) {
      visitor->do_input(slot);
      visitor->do_input(new_val);
    }
  }

  NOT_PRODUCT(virtual void print_name(outputStream* out) const override { out->print("MMTkC1HybridBarrierStub"); });
};

class MMTkHybridBarrierSetC1: public MMTkBarrierSetC1 {
protected:
  virtual void object_reference_write_pre(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const override;

  virtual void load_at_resolved(LIRAccess& access, LIR_Opr result) override;

  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register) override {
    return MMTkBarrierSetC1::resolve_address_in_register(access, resolve_in_register);
  }
};
#else
class MMTkHybridBarrierSetC1;
#endif

//////////////////// C2 ////////////////////

#ifdef COMPILER2
class MMTkHybridBarrierSetC2: public MMTkBarrierSetC2 {
protected:
  virtual void object_reference_write_pre(GraphKit* kit, Node* src, Node* slot, Node* val) const override;

public:
  virtual Node* load_at_resolved(C2Access& access, const Type* val_type) const override;
  virtual void clone(GraphKit* kit, Node* src, Node* dst, Node* size, bool is_array) const override;
};
#else
class MMTkHybridBarrierSetC2;
#endif

//////////////////// Impl ////////////////////

struct MMTkHybridBarrier: MMTkBarrierImpl<
  MMTkHybridBarrierSetRuntime,
  MMTkHybridBarrierSetAssembler,
  MMTkHybridBarrierSetC1,
  MMTkHybridBarrierSetC2
> {};

#endif // MMTK_OPENJDK_BARRIERS_MMTK_HYBRID_BARRIER_HPP
