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
#include "mmtkBarrierSetAssembler_aarch64.hpp"
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
    __ b(slow_case);
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
        __ b(slow_case);
        return;
      }
    } else {
      // var alloc size. We compare with max_non_los_bytes and conditionally jump to slowpath.
      //  printf("max_non_los_bytes %lu\n",max_non_los_bytes);
      __ movi(rscratch1, max_non_los_bytes - extra_header);
      __ tbr(Condition::GT, var_size_in_bytes, rscratch1, slow_case, is_far);
    }

    if (selector.tag == TAG_MALLOC || selector.tag == TAG_LARGE_OBJECT) {
      __ b(slow_case);
      return;
    }

    // Calculate offsets of TLAB top and end
    Address cursor, limit;
    MMTkAllocatorOffsets alloc_offsets = get_tlab_top_and_end_offsets(selector);

    cursor = Address(rthread, alloc_offsets.tlab_top_offset);
    limit = Address(rthread, alloc_offsets.tlab_end_offset);

    // XXX disassembly
    // 0x7fffe85597e0:      ld      a0,688(s7)
    // 0x7fffe85597e4:      add     a1,a0,a3
    // 0x7fffe85597e8:      bltu    a1,a0,0x7fffe8559878
    // 0x7fffe85597ec:      ld      t0,696(s7)
    // 0x7fffe85597f0:      bltu    t0,a1,0x7fffe8559878
    // 0x7fffe85597f4:      sd      a1,688(s7)

    // obj = load lab.cursor
    __ ldr(obj, cursor);
    // end = obj + size
    Register end = tmp2;
    if (var_size_in_bytes == noreg) {
      __ adr(end, Address(obj, con_size_in_bytes));
    } else {
      __ add(end, obj, var_size_in_bytes);
    }
    // slowpath if end < obj
    __ cmp(end, obj);
    __ bltu(slow_case, is_far);
    // slowpath if end > lab.limit
    __ ldr(tmp1, limit);
    // XXX debug use, force slow path
    // __ bgtu(end, zr, slow_case, is_far);
    __ bgtu(end, tmp1, slow_case, is_far);
    // lab.cursor = end
    __ str(end, cursor);

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
  // printf("xxx MMTkBarrierSetAssembler::generate_c1_write_barrier_runtime_stub\n");
  // See also void G1BarrierSetAssembler::generate_c1_post_barrier_runtime_stub(StubAssembler* sasm)
  __ prologue("mmtk_write_barrier", false);

  Label done, runtime;

  // void C1_MacroAssembler::load_parameter(int offset_in_words, Register reg)
  // ld(reg, Address(fp, offset_in_words * BytesPerWord));
  // ra is free to use here, because call prologue/epilogue handles it
  const Register src = rscratch1;
  const Register slot = c_rarg0;
  const Register new_val = c_rarg1;
  __ load_parameter(0, src);
  __ load_parameter(1, slot);
  __ load_parameter(2, new_val);

  __ bind(runtime);

  // Push integer registers x7, x10-x17, x28-x31.
  //                        t2, a0-a7,   t3-t6
  __ push_call_clobbered_registers();

#if MMTK_ENABLE_BARRIER_FASTPATH
  __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), src, slot, new_val);
#else
  __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_reference _write_post_call), src, slot, new_val);
#endif

  __ pop_call_clobbered_registers();

  __ bind(done);

  __ epilogue();
}

#undef __

#define __ ce->masm()->

void MMTkBarrierSetAssembler::generate_c1_write_barrier_stub_call(LIR_Assembler* ce, MMTkC1BarrierStub* stub) {
  // printf("xxx MMTkBarrierSetAssembler::generate_c1_write_barrier_stub_call\n");
  // See also void G1BarrierSetAssembler::gen_post_barrier_stub(LIR_Assembler* ce, G1PostBarrierStub* stub)
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());
  assert(stub->src->is_register(), "Precondition");
  assert(stub->slot->is_register(), "Precondition");
  assert(stub->new_val->is_register(), "Precondition");
  // LIR_Assembler::store_parameter(Register r, int offset_from_rsp_in_words)
  // __ sd(r, Address(sp, offset_from_rsp_in_bytes));
  ce->store_parameter(stub->src->as_pointer_register(), 0);
  ce->store_parameter(stub->slot->as_pointer_register(), 1);
  ce->store_parameter(stub->new_val->as_pointer_register(), 2);
  __ far_call(RuntimeAddress(bs->_write_barrier_c1_runtime_code_blob->code_begin()));
  __ j(*stub->continuation());
}

#undef __
