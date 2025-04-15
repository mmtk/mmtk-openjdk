#ifndef MMTK_OPENJDK_MMTK_OBJECT_BARRIER_SET_ASSEMBLER_X86_HPP
#define MMTK_OPENJDK_MMTK_OBJECT_BARRIER_SET_ASSEMBLER_X86_HPP

class MMTkObjectBarrierSetAssembler: public MMTkBarrierSetAssembler {
protected:
  virtual void object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, bool compensate_val_reg) const override;
public:
  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) override;
  virtual void arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) override;
};
#endif // MMTK_OPENJDK_MMTK_OBJECT_BARRIER_SET_ASSEMBLER_X86_HPP
