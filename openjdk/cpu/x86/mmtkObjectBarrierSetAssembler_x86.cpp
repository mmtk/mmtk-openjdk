#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

#define __ masm->

void MMTkObjectBarrierSetAssembler::object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, bool compensate_val_reg) const {
  if (can_remove_barrier(decorators, val, /* skip_const_null */ true)) return;

  bool is_not_null = (decorators & IS_NOT_NULL) != 0;

  Register obj = dst.base();
#if MMTK_ENABLE_BARRIER_FASTPATH
  Label done;

  Register tmp3 = rscratch1;
  Register tmp4 = rscratch2;
  assert_different_registers(obj, tmp2, tmp3);
  assert_different_registers(tmp4, rcx);

  // tmp2 = load-byte (SIDE_METADATA_BASE_ADDRESS + (obj >> 6));
  __ movptr(tmp3, obj);
  __ shrptr(tmp3, 6);
  __ movptr(tmp2, SIDE_METADATA_BASE_ADDRESS);
  __ movb(tmp2, Address(tmp2, tmp3));
  // tmp3 = (obj >> 3) & 7
  __ movptr(tmp3, obj);
  __ shrptr(tmp3, 3);
  __ andptr(tmp3, 7);
  // tmp2 = tmp2 >> tmp3
  __ movptr(tmp4, rcx);
  __ movl(rcx, tmp3);
  __ shrptr(tmp2);
  __ movptr(rcx, tmp4);
  // if ((tmp2 & 1) == 1) goto slowpath;
  __ andptr(tmp2, 1);
  __ cmpptr(tmp2, 1);
  __ jcc(Assembler::notEqual, done);
#endif

  __ movptr(c_rarg0, obj);
  __ xorptr(c_rarg1, c_rarg1);
  // Note: If `compensate_val_reg == true && UseCompressedOops === true`, the `val` register will be
  // holding a compressed pointer to the target object. If the write barrier needs to know the
  // target, we will need to decompress it before passing it to the barrier slow path. However,
  // since we know the semantics of `mmtk::plan::barriers::ObjectBarrier`, i.e. it logs the object
  // without looking at the `slot` or the `target` parameter at all, we simply pass nullptr to both
  // parameters.
  __ xorptr(c_rarg2, c_rarg2);

#if MMTK_ENABLE_BARRIER_FASTPATH
  __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), 3);
  __ bind(done);
#else
  __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call), 3);
#endif
}

void MMTkObjectBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  // `count` or `dst` register values may get overwritten after the array copy, and `arraycopy_epilogue` can receive invalid addresses.
  // Save the register values here and restore them in `arraycopy_epilogue`.
  // See https://github.com/openjdk/jdk/blob/jdk-11%2B19/src/hotspot/cpu/x86/gc/shared/modRefBarrierSetAssembler_x86.cpp#L37-L50
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool obj_int = type == T_OBJECT LP64_ONLY(&& UseCompressedOops);
  if (type == T_OBJECT || type == T_ARRAY) {
    if (!checkcast) {
      if (!obj_int) {
        // Save count for barrier
        __ movptr(r11, count);
      } else if (disjoint) {
        // Save dst in r11 in the disjoint case
        __ movq(r11, dst);
      }
    }
  }
}

void MMTkObjectBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool obj_int = type == T_OBJECT LP64_ONLY(&& UseCompressedOops);
  const bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if ((type == T_OBJECT || type == T_ARRAY) && !dest_uninitialized) {
    if (!checkcast) {
      if (!obj_int) {
        // Save count for barrier
        count = r11;
      } else if (disjoint) {
        // Use the saved dst in the disjoint case
        dst = r11;
      }
    }
    __ pusha();
    __ movptr(c_rarg0, src);
    __ movptr(c_rarg1, dst);
    __ movptr(c_rarg2, count);
    __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_post_call), 3);
    __ popa();
  }
}

#undef __