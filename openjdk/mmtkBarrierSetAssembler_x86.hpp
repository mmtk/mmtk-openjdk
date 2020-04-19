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
};

#endif // MMTK_MMTKBARRIERSETASSEMBLER_X86_HPP