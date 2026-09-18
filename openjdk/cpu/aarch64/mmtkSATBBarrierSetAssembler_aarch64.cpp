#include "precompiled.hpp"
#include "mmtkSATBBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Assembler ////////////////////

#define __ masm->

void MMTkSATBBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp_thread) {
  bool on_oop = type == T_OBJECT || type == T_ARRAY;
  bool on_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool on_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool on_reference = on_weak || on_phantom;

  BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);

  if (mmtk_enable_reference_load_barrier) {
    if (on_oop && on_reference) {
      Label done;

      assert_different_registers(dst, tmp1);

      // No slow-call if SATB is not active
      // intptr_t tmp1_q = CONCURRENT_MARKING_ACTIVE;
      __ movptr(tmp1, intptr_t(&CONCURRENT_MARKING_ACTIVE));
      // Load with zero extension to 32 bits.
      // uint32_t tmp1_l = (uint32_t)(*(unt8_t*)tmp1_q);
      __ ldrb(tmp1, Address(tmp1, 0));
      // if (tmp1_l == 0) goto done;
      __ cbz(tmp1, done);
      // if (dst == 0) goto done;
      __ cbz(dst, done);
      // Do slow-call
      __ push_call_clobbered_registers();
      __ mov(c_rarg0, dst);
      __ MacroAssembler::call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::load_reference_call), 1);
      __ pop_call_clobbered_registers();
      __ bind(done);
    }
  }
}

void MMTkSATBBarrierSetAssembler::object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) const {
  if (can_remove_barrier(decorators, val, /* skip_const_null */ false)) return;
  object_reference_write_pre_or_post(masm, decorators, dst, val, tmp1, tmp2, tmp3, /* pre = */ true);
}

void MMTkSATBBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop, Register src, Register dst, Register count, RegSet saved_regs) {
  // If the destination is uninitialized (e.g. the Arrays.copyOf / Object.clone fast path that
  // allocates the destination without zeroing it, relying on this very copy to fill it in), its
  // slots do not hold real "old" values to snapshot for SATB. Mirror
  // G1BarrierSetAssembler::gen_write_ref_array_pre_barrier and skip the barrier entirely.
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if (dest_uninitialized) return;
  if (is_oop) {
    Label done;
    // Skip the runtime call if count is zero.
    __ cbz(count, done);
    __ push_call_clobbered_registers();
    __ mov(c_rarg0, src);
    __ mov(c_rarg1, dst);
    __ mov(c_rarg2, count);
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_pre_call), 3);
    __ pop_call_clobbered_registers();
    __ bind(done);
  }
}

#undef __
