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
#include "mmtkMutator.hpp"
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
    // We always use the default allocator.
    // But we need to figure out which allocator we are using by querying MMTk.
    AllocatorSelector selector = get_allocator_mapping(AllocatorDefault);

    // Only bump pointer allocator is implemented.
    if (selector.tag != TAG_BUMP_POINTER) {
      fatal("unimplemented allocator fastpath\n");
    }

    // Calculat offsets of top and end. We now assume we are using bump pointer.
    int allocator_base_offset = in_bytes(JavaThread::third_party_heap_mutator_offset())
      + in_bytes(byte_offset_of(MMTkMutatorContext, allocators))
      + in_bytes(byte_offset_of(Allocators, bump_pointer))
      + selector.index * sizeof(BumpAllocator);

    Address cursor = Address(r15_thread, allocator_base_offset + in_bytes(byte_offset_of(BumpAllocator, cursor)));
    Address limit = Address(r15_thread, allocator_base_offset + in_bytes(byte_offset_of(BumpAllocator, limit)));
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
    __ cmpptr(end, limit);
    __ jcc(Assembler::above, slow_case);
    // lab.cursor = end
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

  BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);

  record_modified_node(masm, dst.base(), tmp1, tmp2);
}

void MMTkBarrierSetAssembler::record_modified_node(MacroAssembler* masm, Register obj, Register tmp1, Register tmp2) {
  Label done;
  Register reg = r9;

  assert_different_registers(obj, tmp2);
  assert_different_registers(obj, tmp2, rax);
  assert_different_registers(obj, tmp2, reg);

  __ push(rax);
  __ push(reg);
  __ push(rcx);

  // const size_t chunk_index = ((size_t) obj) >> MMTK_LOG_CHUNK_SIZE;
  // const size_t chunk_offset = chunk_index << MMTK_LOG_PER_CHUNK_METADATA_SIZE;
  __ movptr(tmp2, obj);
  __ shrptr(tmp2, MMTK_LOG_CHUNK_SIZE);
  __ shlptr(tmp2, MMTK_LOG_PER_CHUNK_METADATA_SIZE);

  // const size_t bit_index = (((size_t) obj) & MMTK_CHUNK_MASK) >> 3;
  // const size_t word_offset = ((bit_index >> 6) << 3);
  Register tmp3 = rax;
  __ movptr(tmp3, obj);
  __ andptr(tmp3, MMTK_CHUNK_MASK);
  __ shrptr(tmp3, 3);
  __ movptr(reg, tmp3);
  __ shrptr(tmp3, 6);
  __ shlptr(tmp3, 3);

  // const size_t word = *((size_t*) (MMTK_HEAP_END + chunk_offset + word_offset));
  __ addptr(tmp2, tmp3);
  __ push(reg);
  __ movptr(reg, (intptr_t) MMTK_HEAP_END);
  __ movptr(tmp2, Address(reg, tmp2));
  __ pop(reg);

  // const size_t bit_offset = bit_index & 63;
  // if ((word & (1ULL << bit_offset)) == 0) { ... }
  __ andptr(reg, 63);
  __ movptr(tmp3, 1);
  __ push(rcx);
  __ movl(rcx, reg);
  __ shlptr(tmp3);
  __ pop(rcx);
  __ andptr(tmp2, tmp3);
  __ cmpptr(tmp2, (int32_t) NULL_WORD);
  __ jcc(Assembler::notEqual, done);

  assert_different_registers(c_rarg0, obj);
  __ movptr(c_rarg0, obj);
  __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkBarrierRuntime::record_modified_node), 1);

  __ bind(done);

  __ pop(rcx);
  __ pop(reg);
  __ pop(rax);
}



#ifdef COMPILER1

#undef __
#define __ ce->masm()->

void MMTkBarrierSetAssembler::gen_write_barrier_stub(LIR_Assembler* ce, MMTkWriteBarrierStub* stub) {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();

  __ bind(*stub->entry());

  ce->store_parameter(stub->src()->as_pointer_register(), 0);
  // ce->store_parameter(stub->slot()->as_pointer_register(), 1);
  // ce->store_parameter(stub->new_val()->as_pointer_register(), 2);

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
  // __ push(c_rarg1);
  // __ push(c_rarg2);
  __ push(rax);

  __ load_parameter(0, c_rarg0);
  // __ load_parameter(1, c_rarg1);
  // __ load_parameter(2, c_rarg2);

  __ bind(runtime);

  __ save_live_registers_no_oop_map(true);

  __ call_VM_leaf_base(CAST_FROM_FN_PTR(address, MMTkBarrierRuntime::record_modified_node), 1);

  __ restore_live_registers(true);

  __ bind(done);
  __ pop(rax);
  // __ pop(c_rarg2);
  // __ pop(c_rarg1);
  __ pop(c_rarg0);

  __ epilogue();
}

#undef __

#endif // COMPILER1
