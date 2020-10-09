#ifndef MMTK_MMTKBARRIERSETASSEMBLER_X86_HPP
#define MMTK_MMTKBARRIERSETASSEMBLER_X86_HPP

#include "asm/macroAssembler.hpp"
#include "gc/shared/barrierSetAssembler.hpp"

class LIR_Assembler;
class StubAssembler;
class MMTkWriteBarrierStub;

class MMTkBarrierSetAssembler: public BarrierSetAssembler {
 public:
  void gen_write_barrier_stub(LIR_Assembler* ce, MMTkWriteBarrierStub* stub);
  void generate_c1_write_barrier_runtime_stub(StubAssembler* sasm);
  virtual void eden_allocate(MacroAssembler* masm, Register thread, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Label& slow_case);

  void write_barrier(MacroAssembler* masm, Register obj, Register slot, Register pre_val);
  void oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Address dst, Register val, Register tmp1, Register tmp2);

  virtual void store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Address dst, Register val, Register tmp1, Register tmp2);

  virtual void arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count);
};

#endif // MMTK_MMTKBARRIERSETASSEMBLER_X86_HPP