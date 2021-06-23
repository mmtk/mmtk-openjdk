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

#define __ masm->

void MMTkBarrierSetAssembler::eden_allocate(MacroAssembler* masm, Register thread, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Label& slow_case) {
  assert(obj == rax, "obj must be in rax, for cmpxchg");
  assert_different_registers(obj, var_size_in_bytes, t1);

  if (!MMTK_ENABLE_ALLOCATION_FASTPATH) {
    __ jmp(slow_case);
  } else {
    // We only use LOS or the default allocator. We need to check size
    // max non-los size
    size_t max_non_los_bytes = get_max_non_los_default_alloc_bytes();
    // For size larger than max non-los size, we always jump to slowpath.
    if (var_size_in_bytes == noreg) {
      assert(con_size_in_bytes <= 128*1024, "con_size_in_bytes should be smaller than 128K");
      // const size: this check seems never true, which means openjdk may have its own size check.
      if ((size_t)con_size_in_bytes > max_non_los_bytes) {
        // printf("consize jump: %d > %ld\n", con_size_in_bytes, max_non_los_bytes);
        // assert(false, "we are actually jumping to slow due to mmtk size check");
        __ jmp(slow_case);
        return;
      }
    } else {
      assert(var_size_in_bytes->is_valid(), "var_size_in_bytes is not noreg, and is not valid");
      // var size
      __ cmpptr(var_size_in_bytes, max_non_los_bytes);
      __ jcc(Assembler::aboveEqual, slow_case);
    }

    // fastpath, we only use default allocator
    Allocator allocator = AllocatorDefault;
    // We need to figure out which allocator we are using by querying MMTk.
    AllocatorSelector selector = get_allocator_mapping(allocator);

    if (selector.tag == TAG_MALLOC || selector.tag == TAG_LARGE_OBJECT) {
      __ jmp(slow_case);
      return;
    }

    // Only bump pointer allocator is implemented.
    if (selector.tag != TAG_BUMP_POINTER) {
      fatal("unimplemented allocator fastpath\n");
    }

    // Calculate offsets of top and end. We now assume we are using bump pointer.
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

