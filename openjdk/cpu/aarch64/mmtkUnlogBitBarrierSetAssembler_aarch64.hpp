#ifndef MMTK_OPENJDK_MMTK_UNLOG_BIT_BARRIER_SET_ASSEMBLER_AARCH64_HPP
#define MMTK_OPENJDK_MMTK_UNLOG_BIT_BARRIER_SET_ASSEMBLER_AARCH64_HPP

#include "../mmtkBarrierSet.hpp"
#include "mmtkUnlogBitBarrier.hpp"

#include "utilities/macros.hpp"
#include CPU_HEADER(mmtkBarrierSetAssembler)

//////////////////// Assembler ////////////////////

class MMTkUnlogBitBarrierSetAssembler: public MMTkBarrierSetAssembler {
protected:
  static void emit_check_unlog_bit_fast_path(MacroAssembler* masm, Label &done, Register obj, Register tmp1, Register tmp2, Register tmp3);
  static void object_reference_write_pre_or_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3, bool pre);

public:
  /// Generate C1 barrier slow path stub
  void generate_c1_unlog_bit_barrier_slow_path_stub(LIR_Assembler* ce, MMTkC1UnlogBitBarrierSlowPathStub* stub) const;
};

#endif // MMTK_OPENJDK_MMTK_UNLOG_BIT_BARRIER_SET_ASSEMBLER_AARCH64_HPP
