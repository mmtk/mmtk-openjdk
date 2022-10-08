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
#include "mmtkBarrierSetAssembler_riscv.hpp"
#include "mmtkBarrierSetC1.hpp"
#include "mmtkMutator.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/macros.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"

#define __ masm->

void MMTkBarrierSetAssembler::eden_allocate(MacroAssembler* masm, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register tmp1, Register tmp2, Label& slow_case, bool is_far) {
  // XXX tmp1 seems to be -1
  assert_different_registers(obj, tmp2);
  assert_different_registers(obj, var_size_in_bytes);
  assert(tmp2->is_valid(), "need temp reg");

  if (!MMTK_ENABLE_ALLOCATION_FASTPATH) {
    __ j(slow_case);
  } else {
    //  printf("generating mmtk allocation fast path\n");
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

    // XXX riscv: disallow markcompact and global alloc bit for now
    assert(selector.tag != TAG_MARK_COMPACT, "mark compact not supported for now");

    if (var_size_in_bytes == noreg) {
      // constant alloc size. If it is larger than max_non_los_bytes, we directly go to slowpath.
      if ((size_t)con_size_in_bytes > max_non_los_bytes - extra_header) {
        __ j(slow_case);
        return;
      }
    } else {
      // var alloc size. We compare with max_non_los_bytes and conditionally jump to slowpath.
      //  printf("max_non_los_bytes %lu\n",max_non_los_bytes);
      __ li(t0, max_non_los_bytes - extra_header);
      __ bgeu(var_size_in_bytes, t0, slow_case, is_far);
    }

    if (selector.tag == TAG_MALLOC || selector.tag == TAG_LARGE_OBJECT) {
      __ j(slow_case);
      return;
    }

    // Calculate offsets of TLAB top and end
    Address cursor, limit;
    MMTkAllocatorOffsets alloc_offsets = get_tlab_top_and_end_offsets(selector);

    cursor = Address(xthread, alloc_offsets.tlab_top_offset);
    limit = Address(xthread, alloc_offsets.tlab_end_offset);

    // XXX disassembly
    // 0x7fffe85597e0:      ld      a0,688(s7)
    // 0x7fffe85597e4:      add     a1,a0,a3
    // 0x7fffe85597e8:      bltu    a1,a0,0x7fffe8559878
    // 0x7fffe85597ec:      ld      t0,696(s7)
    // 0x7fffe85597f0:      bltu    t0,a1,0x7fffe8559878
    // 0x7fffe85597f4:      sd      a1,688(s7)

    // obj = load lab.cursor
    __ ld(obj, cursor);
    // end = obj + size
    Register end = tmp2;
    if (var_size_in_bytes == noreg) {
      __ la(end, Address(obj, con_size_in_bytes));
    } else {
      __ add(end, obj, var_size_in_bytes);
    }
    // slowpath if end < obj
    __ bltu(end, obj, slow_case, is_far);
    // slowpath if end > lab.limit
    __ ld(t0, limit);
    // XXX debug use, force slow path
    // __ bgtu(end, zr, slow_case, is_far);
    __ bgtu(end, t0, slow_case, is_far);
    // lab.cursor = end
    __ sd(end, cursor);

    // recover var_size_in_bytes if necessary
    if (var_size_in_bytes == end) {
      __ sub(var_size_in_bytes, var_size_in_bytes, obj);
    }
    // if the above is removed, and the register holding the object size is
    // clobbered, operations that rely on the size, such as array copy will
    // crash

    // XXX debug use, force segfault to disassemble in gdb
    // __ ld(t0, zr);

    // XXX debug use, force double allocation
    // __ j(slow_case);

  #ifdef MMTK_ENABLE_GLOBAL_ALLOC_BIT
    assert(false, "global alloc bit not supported");
  #endif
  }
}

#undef __

#define __ sasm->

void MMTkBarrierSetAssembler::generate_c1_write_barrier_runtime_stub(StubAssembler* sasm) const {
  // assert(false, "Not implemented");
}

#undef __

#define __ ce->masm()->

void MMTkBarrierSetAssembler::generate_c1_write_barrier_stub_call(LIR_Assembler* ce, MMTkC1BarrierStub* stub) {
//  assert(false, "Not implemented");
}

#undef __
