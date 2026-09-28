#ifndef MMTK_OPENJDK_MMTK_FIELD_BARRIER_SET_ASSEMBLER_AARCH64_HPP
#define MMTK_OPENJDK_MMTK_FIELD_BARRIER_SET_ASSEMBLER_AARCH64_HPP

#include "utilities/macros.hpp"
#include CPU_HEADER(mmtkBarrierSetAssembler)

struct MMTkC1FieldBarrierStub;

//////////////////// Assembler ////////////////////

class MMTkFieldBarrierSetAssembler: public MMTkBarrierSetAssembler {
protected:
  virtual void object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) const override;
public:
  virtual void generate_c1_pre_write_barrier_stub(LIR_Assembler* ce, MMTkC1FieldBarrierStub* stub) const;
  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop, Register src, Register dst, Register count, RegSet saved_regs) override;
  virtual void load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp2) override;
};

#endif // MMTK_OPENJDK_MMTK_FIELD_BARRIER_SET_ASSEMBLER_AARCH64_HPP
