#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

#define __ masm->

void MMTkObjectBarrierSetAssembler::object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const {
  assert(false, "Not implemented");
}

void MMTkObjectBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  assert(false, "Not implemented");
}

#undef __