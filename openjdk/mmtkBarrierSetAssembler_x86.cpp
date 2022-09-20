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
#include "interpreter/interp_masm.hpp"
#include "mmtkBarrierSet.hpp"
#include "mmtkBarrierSetAssembler_x86.hpp"
#include "mmtkBarrierSetC1.hpp"
#include "mmtkMutator.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/macros.hpp"
#include "c1/c1_LIRAssembler.hpp"

#define __ masm->

void MMTkBarrierSetAssembler::eden_allocate(MacroAssembler* masm, Register thread, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Label& slow_case) {
  assert(obj == rax, "obj must be in rax, for cmpxchg");
  assert_different_registers(obj, var_size_in_bytes, t1);

  if (!MMTK_ENABLE_ALLOCATION_FASTPATH) {
    __ jmp(slow_case);
  } else {
    // MMTk size check. If the alloc size is larger than the allowed max size for non los,
    // we jump to slow path and allodate with LOS in slowpath.
    // Note that OpenJDK has a slow path check. Search for layout_helper_needs_slow_path and FastAllocateSizeLimit.
    // I tried to set FastAllocateSizeLimit in MMTkHeap::initialize(). But there are still large objects allocated into the
    // default space.
    assert(MMTkMutatorContext::max_non_los_default_alloc_bytes != 0, "max_non_los_default_alloc_bytes hasn't been initialized");
    size_t max_non_los_bytes = MMTkMutatorContext::max_non_los_default_alloc_bytes;
    size_t extra_header = 0;
    // fastpath, we only use default allocator
    Allocator allocator = AllocatorDefault;
    // We need to figure out which allocator we are using by querying MMTk.
    AllocatorSelector selector = get_allocator_mapping(allocator);
    if (selector.tag == TAG_MARK_COMPACT) extra_header = MMTK_MARK_COMPACT_HEADER_RESERVED_IN_BYTES;

    if (var_size_in_bytes == noreg) {
      // constant alloc size. If it is larger than max_non_los_bytes, we directly go to slowpath.
      if ((size_t)con_size_in_bytes > max_non_los_bytes - extra_header) {
        __ jmp(slow_case);
        return;
      }
    } else {
      // var alloc size. We compare with max_non_los_bytes and conditionally jump to slowpath.
      __ cmpptr(var_size_in_bytes, max_non_los_bytes - extra_header);
      __ jcc(Assembler::aboveEqual, slow_case);
    }

    if (selector.tag == TAG_MALLOC || selector.tag == TAG_LARGE_OBJECT) {
      __ jmp(slow_case);
      return;
    }

    // Calculate offsets of TLAB top and end
    Address cursor, limit;
    MMTkAllocatorOffsets alloc_offsets = get_tlab_top_and_end_offsets(selector);

    cursor = Address(r15_thread, alloc_offsets.tlab_top_offset);
    limit = Address(r15_thread, alloc_offsets.tlab_end_offset);

    // obj = load lab.cursor
    __ movptr(obj, cursor);
    if (selector.tag == TAG_MARK_COMPACT) __ addptr(obj, extra_header);
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
  bool enable_global_alloc_bit = false;
  #ifdef MMTK_ENABLE_GLOBAL_ALLOC_BIT
  enable_global_alloc_bit = true;
  #endif
  if (enable_global_alloc_bit || selector.tag == TAG_MARK_COMPACT) {
    Register tmp3 = rdi;
    Register tmp2 = rscratch1;
    assert_different_registers(obj, tmp2, tmp3, rcx);

    // tmp2 = load-byte (SIDE_METADATA_BASE_ADDRESS + (obj >> 6));
    __ movptr(tmp3, obj);
    __ shrptr(tmp3, 6);
    __ movptr(tmp2, ALLOC_BIT_BASE_ADDRESS);
    __ movb(tmp2, Address(tmp2, tmp3));
    // tmp3 = 1 << ((obj >> 3) & 7)
    //   1. rcx = (obj >> 3) & 7
    __ movptr(rcx, obj);
    __ shrptr(rcx, 3);
    __ andptr(rcx, 7);
    //   2. tmp3 = 1 << rcx
    __ movptr(tmp3, 1);
    __ shlptr(tmp3);
    // tmp2 = tmp2 | tmp3
    __ orptr(tmp2, tmp3);

    // store-byte tmp2 (SIDE_METADATA_BASE_ADDRESS + (obj >> 6))
    __ movptr(tmp3, obj);
    __ shrptr(tmp3, 6);
    __ movptr(rcx, ALLOC_BIT_BASE_ADDRESS);
    __ movb(Address(rcx, tmp3), tmp2);
  }

    // BarrierSetAssembler::incr_allocated_bytes
    if (var_size_in_bytes->is_valid()) {
      __ addq(Address(r15_thread, in_bytes(JavaThread::allocated_bytes_offset())), var_size_in_bytes);
    } else {
      __ addq(Address(r15_thread, in_bytes(JavaThread::allocated_bytes_offset())), con_size_in_bytes);
    }
    __ addq(Address(r15_thread, in_bytes(JavaThread::allocated_bytes_offset())), extra_header);
  }
}

#undef __

#define __ sasm->

void MMTkBarrierSetAssembler::generate_c1_write_barrier_runtime_stub(StubAssembler* sasm) const {
  __ prologue("mmtk_write_barrier", false);

  Address store_addr(rbp, 4*BytesPerWord);

  Label done, runtime;

  __ push(c_rarg0);
  __ push(c_rarg1);
  __ push(c_rarg2);
  __ push(rax);

  __ load_parameter(0, c_rarg0);
  __ load_parameter(1, c_rarg1);
  __ load_parameter(2, c_rarg2);

  __ bind(runtime);

  __ save_live_registers_no_oop_map(true);

#if MMTK_ENABLE_BARRIER_FASTPATH
  __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), 3);
#else
  __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call), 3);
#endif

  __ restore_live_registers(true);

  __ bind(done);
  __ pop(rax);
  __ pop(c_rarg2);
  __ pop(c_rarg1);
  __ pop(c_rarg0);

  __ epilogue();
}

#undef __

#define __ ce->masm()->

void MMTkBarrierSetAssembler::generate_c1_write_barrier_stub_call(LIR_Assembler* ce, MMTkC1BarrierStub* stub) {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());
  ce->store_parameter(stub->src->as_pointer_register(), 0);
  ce->store_parameter(stub->slot->as_pointer_register(), 1);
  ce->store_parameter(stub->new_val->as_pointer_register(), 2);
  __ call(RuntimeAddress(bs->_write_barrier_c1_runtime_code_blob->code_begin()));
  __ jmp(*stub->continuation());
}

#undef __
