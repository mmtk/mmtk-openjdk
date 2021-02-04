#ifndef MMTK_MMTKBARRIERSETASSEMBLER_X86_HPP
#define MMTK_MMTKBARRIERSETASSEMBLER_X86_HPP

#include "asm/macroAssembler.hpp"
#include "gc/shared/barrierSetAssembler.hpp"

class LIR_Assembler;
class StubAssembler;
class MMTkWriteBarrierStub;

class MMTkBarrierSetAssembler: public BarrierSetAssembler {
public:
  virtual void eden_allocate(MacroAssembler* masm, Register thread, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Label& slow_case);
  virtual void store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Address dst, Register val, Register tmp1, Register tmp2) {
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
  }
};

#endif // MMTK_MMTKBARRIERSETASSEMBLER_X86_HPP