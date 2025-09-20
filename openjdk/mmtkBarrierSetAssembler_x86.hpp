#ifndef MMTK_OPENJDK_MMTK_BARRIER_SET_ASSEMBLER_X86_HPP
#define MMTK_OPENJDK_MMTK_BARRIER_SET_ASSEMBLER_X86_HPP

#include "asm/macroAssembler.hpp"
#include "gc/shared/barrierSetAssembler.hpp"

class MMTkBarrierSetC1;
class MMTkC1PreBarrierStub;
class MMTkC1PostBarrierStub;
class MMTkC1ReferenceLoadBarrierStub;
class LIR_Assembler;
class StubAssembler;

class MMTkBarrierSetAssembler: public BarrierSetAssembler {
  friend class MMTkBarrierSetC1;

private:
  /// Generate C1 pre or post write barrier slow-call assembly code
  void generate_c1_pre_or_post_write_barrier_runtime_stub(StubAssembler* sasm, bool pre);
  /// Generate C1 weak reference load slow-call assembly code
  void generate_c1_ref_load_barrier_runtime_stub(StubAssembler* sasm) const;

protected:
  /// Full pre-barrier
  virtual void object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const {}
  /// Full post-barrier
  /// `compensate_val_reg` is true if this function is called after `BarrierSetAssembler::store_at` which compresses the pointer in the `val` register in place.
  virtual void object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, bool compensate_val_reg) const {}

  /// Barrier elision test
  virtual bool can_remove_barrier(DecoratorSet decorators, Register val, bool skip_const_null) const {
    bool in_heap = (decorators & IN_HEAP) != 0;
    bool as_normal = (decorators & AS_NORMAL) != 0;
    assert((decorators & IS_DEST_UNINITIALIZED) == 0, "unsupported");
    return !in_heap || (skip_const_null && val == noreg);
  }

public:
  virtual void eden_allocate(MacroAssembler* masm, Register thread, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Label& slow_case) override;
  virtual void store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Address dst, Register val, Register tmp1, Register tmp2) override {
    if (type == T_OBJECT || type == T_ARRAY) object_reference_write_pre(masm, decorators, dst, val, tmp1, tmp2);
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    // BarrierSetAssembler::store_at modifies val and make it compressed if UseCompressedOops is true.
    // We need to compensate for this change and decode it in object_reference_write_post.
    if (type == T_OBJECT || type == T_ARRAY) object_reference_write_post(masm, decorators, dst, val, tmp1, tmp2, true);
  }

  /// Generate C1 write barrier slow-call stub
  virtual void generate_c1_pre_write_barrier_stub(LIR_Assembler* ce, MMTkC1PreBarrierStub* stub) const {};
  virtual void generate_c1_post_write_barrier_stub(LIR_Assembler* ce, MMTkC1PostBarrierStub* stub) const {};
  static void generate_c1_ref_load_barrier_stub_call(LIR_Assembler* ce, MMTkC1ReferenceLoadBarrierStub* stub);
};
#endif // MMTK_OPENJDK_MMTK_BARRIER_SET_ASSEMBLER_X86_HPP
