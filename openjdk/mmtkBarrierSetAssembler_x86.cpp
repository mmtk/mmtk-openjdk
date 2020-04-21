/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "mmtkBarrierSet.hpp"
#include "mmtkBarrierSetAssembler_x86.hpp"
#include "interpreter/interp_masm.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/macros.hpp"
#ifdef COMPILER1
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "mmtkBarrierSetC1.hpp"
#endif

#define __ masm->

void MMTkBarrierSetAssembler::eden_allocate(MacroAssembler* masm, Register thread, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Label& slow_case) {
  assert(obj == rax, "obj must be in rax, for cmpxchg");
  assert_different_registers(obj, var_size_in_bytes, t1);
  // printf("eden_allocate\n");
  if (!MMTK_ENABLE_ALLOCATION_FASTPATH) {
    __ jmp(slow_case);
  } else {
    Address cursor = Address(r15_thread, in_bytes(JavaThread::third_party_heap_mutator_offset()) + 8 * 1);
    Address limit = Address(r15_thread, in_bytes(JavaThread::third_party_heap_mutator_offset()) + 8 * 2);
    // obj = load lab.cursor
    __ movptr(obj, cursor);
    // end = obj + size
    Register end = t1;
    if (var_size_in_bytes == noreg) {
      __ lea(end, Address(obj, con_size_in_bytes));
    } else {
      __ lea(end, Address(obj, var_size_in_bytes, Address::times_1));
    }
    // slowpath if end < obj
    __ cmpptr(end, obj);
    __ jcc(Assembler::below, slow_case);
    // slowpath if end > lab.limit
    // FIXME: We are lack of temp registers here... we have to push obj to stack
    __ push(obj);
    __ movptr(obj, limit);
    __ cmpptr(end, obj);
    __ pop(obj);
    __ jcc(Assembler::above, slow_case);
    // lab.limit = end
    __ movptr(cursor, end);
    // BarrierSetAssembler::incr_allocated_bytes
    if (var_size_in_bytes->is_valid()) {
      __ addq(Address(r15_thread, in_bytes(JavaThread::allocated_bytes_offset())), var_size_in_bytes);
    } else {
      __ addq(Address(r15_thread, in_bytes(JavaThread::allocated_bytes_offset())), con_size_in_bytes);
    }
  }
}

void MMTkBarrierSetAssembler::store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                         Address dst, Register val, Register tmp1, Register tmp2) {
  if (MMTK_ENABLE_WRITE_BARRIER && (type == T_OBJECT || type == T_ARRAY)) {
    oop_store_at(masm, decorators, type, dst, val, tmp1, tmp2);
  } else {
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
  }
}

void MMTkBarrierSetAssembler::oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                                Address dst, Register val, Register tmp1, Register tmp2) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool as_normal = (decorators & AS_NORMAL) != 0;
  assert((decorators & IS_DEST_UNINITIALIZED) == 0, "unsupported");

  if (!in_heap || val == noreg) {
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    return;
  }
  
  if (dst.index() == noreg && dst.disp() == 0) {
    if (dst.base() != tmp1) {
      __ movptr(tmp1, dst.base());
    }
  } else {
    __ lea(tmp1, dst);
  }
  
  BarrierSetAssembler::store_at(masm, decorators, type, Address(tmp1, 0), val, noreg, noreg);

  write_barrier(masm /*masm*/, dst.base(), tmp1, val);
}


void MMTkBarrierSetAssembler::write_barrier(MacroAssembler* masm, Register obj, Register slot, Register val) {
  Label done;
  Label runtime;

  __ bind(runtime);
  
  __ mov(c_rarg0, obj);
  __ mov(c_rarg1, slot);
  __ mov(c_rarg2, val);

  __ push(obj);
  __ push(slot);
  __ push(val);

  __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkBarrierRuntime::write_barrier_slow), 3);

  __ pop(val);
  __ pop(slot);
  __ pop(obj);

  __ bind(done);
}



#ifdef COMPILER1

#undef __
#define __ ce->masm()->

void MMTkBarrierSetAssembler::gen_write_barrier_stub(LIR_Assembler* ce, MMTkWriteBarrierStub* stub) {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();

  __ bind(*stub->entry());

  ce->store_parameter(stub->src()->as_pointer_register(), 0);
  ce->store_parameter(stub->slot()->as_pointer_register(), 1);
  ce->store_parameter(stub->new_val()->as_pointer_register(), 2);

  __ call(RuntimeAddress(bs->write_barrier_c1_runtime_code_blob()->code_begin()));
  __ jmp(*stub->continuation());

}

#undef __

#define __ sasm->


void MMTkBarrierSetAssembler::generate_c1_write_barrier_runtime_stub(StubAssembler* sasm) {
  __ prologue("mmtk_write_barrier", false);

  Address store_addr(rbp, 3*BytesPerWord);

  Label done;
  Label runtime;

  __ push(c_rarg0);
  __ push(c_rarg1);
  __ push(c_rarg2);
  __ push(rax);

  __ load_parameter(0, c_rarg0);
  __ load_parameter(1, c_rarg1);
  __ load_parameter(2, c_rarg2);

  __ bind(runtime);

  __ save_live_registers_no_oop_map(true);

  __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkBarrierRuntime::write_barrier_slow), 3);

  __ restore_live_registers(true);

  __ bind(done);
  __ pop(rax);
  __ pop(c_rarg2);
  __ pop(c_rarg1);
  __ pop(c_rarg0);

  __ epilogue();
}

#undef __

#endif // COMPILER1
