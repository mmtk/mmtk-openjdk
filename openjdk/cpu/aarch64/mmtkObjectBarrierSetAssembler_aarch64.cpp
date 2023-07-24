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
  // For do_oop_store, we have three tmps, x28/t3, x29/t4, x13/a3
  // printf("object_reference_write_post\n");
//   if (can_remove_barrier(decorators, val, /* skip_const_null */ true)) return;
  if (can_remove_barrier(decorators, val, /* skip_const_null */ true)) return;
  Register obj = dst.base();
  
#if MMTK_ENABLE_BARRIER_FASTPATH
  Label done;
  assert_different_registers(obj, tmp1, tmp2);
  assert_different_registers(val, tmp1, tmp2);
  assert(tmp1->is_valid(), "need temp reg");
  assert(tmp2->is_valid(), "need temp reg");
  // tmp1 = load-byte (SIDE_METADATA_BASE_ADDRESS + (obj >> 6));
  __ mov(tmp1, obj);
  __ lsr(tmp1, tmp1, 6); // tmp1 = obj >> 6;
  __ movi(tmp2, SIDE_METADATA_BASE_ADDRESS);
  __ add(tmp1, tmp1, tmp2); // tmp1 = SIDE_METADATA_BASE_ADDRESS + (obj >> 6);
  __ ldrb(tmp1, Address(tmp1, 0));
  // tmp2 = (obj >> 3) & 7
  __ mov(tmp2, obj);
  __ lsr(tmp2, tmp2, 3);
  __ andr(tmp2, tmp2, 7);
  // tmp1 = tmp1 >> tmp2
  __ lsrv(tmp1, tmp1, tmp2);
  // if ((tmp1 & 1) == 1) fall through to slowpath;
  // equivalently ((tmp1 & 1) == 0) go to done
  __ andr(tmp1, tmp1, 1);
  __ cbz(tmp1, done);
  // setup calling convention
  __ mov(c_rarg0, obj);
  __ lea(c_rarg1, dst);
  __ mov(c_rarg2, val == noreg ? zr : val);
  __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), 3);

  __ bind(done);
#else
  __ mov(c_rarg0, obj);
  __ lea(c_rarg1, dst);
  __ mov(c_rarg2, val == noreg ? zr : val);
  __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call), 3);
#endif
}

void MMTkObjectBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop,
                                  Register src, Register dst, Register count, Register tmp, RegSet saved_regs) {
  // see also void G1BarrierSetAssembler::gen_write_ref_array_post_barrier
  assert_different_registers(src, dst, count);
  // const bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  // if (is_oop && !dest_uninitialized) {
  if (is_oop){
    __ push(saved_regs, sp);
    __ mov(c_rarg0, src);
    __ mov(c_rarg1, dst);
    __ mov(c_rarg2, count);
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_array_copy_post_call), 3);
    __ pop(saved_regs, sp);
  }
}


#undef __