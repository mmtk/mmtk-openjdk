#ifndef MMTK_OPENJDK_MMTK_BARRIER_SET_ASSEMBLER_RISCV_HPP
#define MMTK_OPENJDK_MMTK_BARRIER_SET_ASSEMBLER_RISCV_HPP

#include "asm/macroAssembler.hpp"
#include "gc/shared/barrierSetAssembler.hpp"

class MMTkBarrierSetC1;
class MMTkC1BarrierStub;
class LIR_Assembler;
class StubAssembler;

class MMTkBarrierSetAssembler: public BarrierSetAssembler {
  friend class MMTkBarrierSetC1;

protected:
  /// Full pre-barrier
  virtual void object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const {}
  /// Full post-barrier
  virtual void object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const {}

  /// Barrier elision test
  virtual bool can_remove_barrier(DecoratorSet decorators, Register val, bool skip_const_null) const {
    bool in_heap = (decorators & IN_HEAP) != 0;
    bool as_normal = (decorators & AS_NORMAL) != 0;
    assert((decorators & IS_DEST_UNINITIALIZED) == 0, "unsupported");
    return !in_heap || (skip_const_null && val == noreg);
  }

  /// Generate C1 write barrier slow-call assembly code
  virtual void generate_c1_write_barrier_runtime_stub(StubAssembler* sasm) const;

public:
  virtual void eden_allocate(MacroAssembler* masm,
    Register obj,                      // result: pointer to object after successful allocation
    Register var_size_in_bytes,        // object size in bytes if unknown at compile time; invalid otherwise
    int      con_size_in_bytes,        // object size in bytes if   known at compile time
    Register tmp1,                     // temp register
    Register tmp2,                     // temp register
    Label&   slow_case,                // continuation point if fast allocation fails
    bool is_far = false
  );
  virtual void store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) {
    if (type == T_OBJECT || type == T_ARRAY) object_reference_write_pre(masm, decorators, dst, val, tmp1, tmp2);
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2, tmp3);
    if (type == T_OBJECT || type == T_ARRAY) object_reference_write_post(masm, decorators, dst, val, tmp1, tmp2);
  }

  /// Generate C1 write barrier slow-call stub
  static void generate_c1_write_barrier_stub_call(LIR_Assembler* ce, MMTkC1BarrierStub* stub);
};
#endif // MMTK_OPENJDK_MMTK_BARRIER_SET_ASSEMBLER_RISCV_HPP
