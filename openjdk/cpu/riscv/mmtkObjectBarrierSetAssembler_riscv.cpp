#include "precompiled.hpp"
#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

#define __ masm->

// extern void object_reference_write_slow_call(void* obj, void* dst, uint64_t val);

// void write_post(char* SIDE_METADATA_BASE_ADDRESS, void* obj, uint64_t val) {
//     char tmp2 = *(SIDE_METADATA_BASE_ADDRESS + ((uint64_t)obj >> 6));
//     char tmp3 = ((uint64_t)obj >> 3) & 7;
//     tmp2 = tmp2 >> tmp3;
//     if ((tmp2 & 1) == 1) {
//         object_reference_write_slow_call(obj, 0, val);
//     }
// }

// write_post(char*, void*, unsigned long):
//         srli    a5,a1,6
//         add     a0,a0,a5
//         lbu     a5,0(a0)
//         srli    a3,a1,3
//         andi    a3,a3,7
//         sraw    a5,a5,a3
//         andi    a5,a5,1
//         mv      a4,a1
//         bne     a5,zero,.L4
//         ret
// .L4:
//         li      a1,0
//         mv      a0,a4
//         tail    _Z32object_reference_write_slow_callPvS_m
void MMTkObjectBarrierSetAssembler::object_reference_write_post(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2) const {
  // tmp1 and tmp2 is from MacroAssembler::access_store_at
  // See tmplateTable_riscv, we don't actually get any temporary register.
  // printf("object_reference_write_post\n");
  if (can_remove_barrier(decorators, val, /* skip_const_null */ true)) return;
#if MMTK_ENABLE_BARRIER_FASTPATH
  Label done;

  Register obj = dst.base();
  assert(obj->is_valid(), "dst must be an offset from a base register");
  assert_different_registers(obj, t0, t1);
  assert_different_registers(val, t0, t1);

  // t0 = load-byte (SIDE_METADATA_BASE_ADDRESS + (obj >> 6));
  __ mv(t0, obj);
  __ srli(t0, t0, 6); // t0 = obj >> 6;
  __ li(t1, SIDE_METADATA_BASE_ADDRESS);
  __ add(t0, t0, t1); // t0 = SIDE_METADATA_BASE_ADDRESS + (obj >> 6);
  __ lbu(t0, Address(t0, 0));
  // t1 = (obj >> 3) & 7
  __ mv(t1, obj);
  __ srli(t1, t1, 3);
  __ andi(t1, t1, 7);
  // t0 = t0 >> t1
  __ sraw(t0, t0, t1);
  // if ((t0 & 1) == 1) fall through to slowpath;
  __ andi(t0, t0, 1);
  __ bnez(t0, done); // (t0 & 1) == 1 is equivalent to (t0 & 1) != 0
  // setup calling convention
  __ mv(c_rarg0, obj);
  __ la(c_rarg1, dst);
  __ mv(c_rarg2, val == noreg ? zr : val);
  __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), 3);

  __ bind(done);
#else
  __ mv(c_rarg0, obj);
  __ la(c_rarg1, dst);
  __ mv(c_rarg2, val == noreg ? zr : val);
  __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call), 3);
#endif
}

void MMTkObjectBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop,
                                  Register src, Register dst, Register count, Register tmp, RegSet saved_regs) {
  // see also void G1BarrierSetAssembler::gen_write_ref_array_post_barrier
  assert_different_registers(src, dst, count);
  const bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if (is_oop && !dest_uninitialized) {
    // in address generate_checkcast_copy, caller tells us to save count
    __ push_reg(saved_regs, sp);
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_post_call), src, dst, count);
    __ pop_reg(saved_regs, sp);
  }
}

#undef __