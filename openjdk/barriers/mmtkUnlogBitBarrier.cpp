#include "precompiled.hpp"
#include "mmtkUnlogBitBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

#define __ masm->

void MMTkUnlogBitBarrierSetAssembler::emit_check_unlog_bit_fast_path(MacroAssembler* masm, Label &done, Register obj, Register tmp1, Register tmp2) {
  assert_different_registers(obj, tmp1, tmp2);

  // tmp2 = load-byte (UNLOG_BIT_BASE_ADDRESS + (obj >> 6));
  __ movptr(tmp1, obj);
  __ shrptr(tmp1, 6);
  __ movptr(tmp2, (intptr_t)UNLOG_BIT_BASE_ADDRESS);
  __ movb(tmp2, Address(tmp2, tmp1));
  // tmp1 = (obj >> 3) & 7
  __ movptr(tmp1, obj);
  __ shrptr(tmp1, 3);
  __ andptr(tmp1, 7);
  // tmp2 = tmp2 >> tmp1
  __ xchgptr(tmp1, rcx);
  __ shrptr(tmp2);
  __ xchgptr(tmp1, rcx);
  // if ((tmp2 & 1) == 0) goto done;
  __ testptr(tmp2, 1);
  __ jcc(Assembler::zero, done);
}

#undef __
