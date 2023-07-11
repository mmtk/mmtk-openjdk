#ifndef MMTK_OPENJDK_MMTK_OBJECT_BARRIER_SET_ASSEMBLER_AARCH64_HPP
#define MMTK_OPENJDK_MMTK_OBJECT_BARRIER_SET_ASSEMBLER_AARCH64_HPP

class MMTkObjectBarrierSetAssembler: public MMTkBarrierSetAssembler {
protected:
  virtual void object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const override;
public:
  virtual void arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop,
                                  Register src, Register dst, Register count, Register tmp, RegSet saved_regs) override;
};
#endif // MMTK_OPENJDK_MMTK_OBJECT_BARRIER_SET_ASSEMBLER_RISCV_HPP
