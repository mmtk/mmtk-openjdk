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
#include "mmtkBarrierSetC1.hpp"
#include "mmtkMutator.hpp"
#include "mmtkHeap.hpp"
#include "gc/shared/tlab_globals.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/macros.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"

#include "utilities/macros.hpp"
#include CPU_HEADER(mmtkBarrierSetAssembler)

#define __ masm->

void MMTkBarrierSetAssembler::eden_allocate(MacroAssembler* masm, Register thread, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Label& slow_case) {
  assert(obj == rax, "obj must be in rax, for cmpxchg");
  assert_different_registers(obj, var_size_in_bytes, t1);

  if (!mmtk_enable_allocation_fastpath) {
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
    if (selector.tag == TAG_LISP2) extra_header = MMTK_LISP2_HEADER_RESERVED_IN_BYTES;

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

    const MMTkSpanFastPathInfo* span_info = NULL;
    if (selector.tag == TAG_SPAN) {
      span_info = mmtk_span_fastpath();
      if (span_info == NULL) {
        __ jmp(slow_case);
        return;
      }
    }

    if (selector.tag == TAG_MALLOC || selector.tag == TAG_LARGE_OBJECT || selector.tag == TAG_FREE_LIST) {
      __ jmp(slow_case);
      return;
    }

    if (selector.tag == TAG_SPAN) {
      span_allocate(masm, span_info, obj, var_size_in_bytes, con_size_in_bytes, t1, slow_case);
    } else {
    // Calculate offsets of TLAB top and end
    Address cursor, limit;
    MMTkAllocatorOffsets alloc_offsets = get_tlab_top_and_end_offsets(selector);

    cursor = Address(r15_thread, alloc_offsets.tlab_top_offset);
    limit = Address(r15_thread, alloc_offsets.tlab_end_offset);

    // obj = load lab.cursor
    __ movptr(obj, cursor);
    if (selector.tag == TAG_LISP2) __ addptr(obj, extra_header);
    // end = obj + size
    Register end = t1;
    if (var_size_in_bytes == noreg) {
      __ lea(end, Address(obj, con_size_in_bytes));
    } else {
      __ lea(end, Address(obj, var_size_in_bytes, Address::times_1));
    }
    // slowpath if end < obj
    // __ cmpptr(end, obj);
    // __ jcc(Assembler::below, slow_case);
    // slowpath if end > lab.limit
    __ cmpptr(end, limit);
    __ jcc(Assembler::above, slow_case);
    // lab.cursor = end
    __ movptr(cursor, end);
    }
  bool enable_vo_bit = false;
  #ifdef MMTK_ENABLE_VO_BIT
  enable_vo_bit = true;
  #endif
  if (enable_vo_bit || selector.tag == TAG_LISP2) {
    Register tmp3 = rdi;
    Register tmp2 = rscratch1;
    assert_different_registers(obj, tmp2, tmp3, rcx);

    // tmp2 = load-byte (SIDE_METADATA_BASE_ADDRESS + (obj >> 6));
    __ movptr(tmp3, obj);
    __ shrptr(tmp3, 6);
    __ movptr(tmp2, vo_bit_base_address());
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
    __ movptr(rcx, vo_bit_base_address());
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

// The SpanAllocator fast path, Go's nextFreeFast (runtime/malloc.go) as in mmtk-core's
// Span::next_free_fast, and Go's gcmarknewobject while marking:
//
//   black = space.allocate_as_live
//   if black && !space.inline_black: slow       // new objects also need log bits (SATB option)
//   span = mutator.span_allocator.alloc[size_to_span_class[size >> 3]]
//   cache = span.alloc_cache
//   if cache == 0: slow
//   bit = ctz(cache)
//   result = span.freeindex + bit
//   if result >= span.nelems: slow
//   freeidx = result + 1
//   if freeidx % 64 == 0 && freeidx != span.nelems: slow
//   span.alloc_cache = cache >> (bit + 1)        // two shifts: a shift by 64 gives 0
//   span.freeindex = freeidx
//   span.alloc_count += 1                        // only the owning mutator writes a cached span
//   if black:                                    // gcmarknewobject; both bits are known to be clear
//     if span.black_scan: atomic span.scan_bits[result / 64] |= 1 << (result % 64)
//     atomic span.mark_bits[result / 64] |= 1 << (result % 64)
//     mutator.span_allocator.black_bytes += span.elemsize   // Go: gcw.bytesMarked
//   obj = span.start + result * span.elemsize
//
// Nothing is written before the last check. The black path needs BMI2 (shlx); without it, the
// fast path bails out while allocating black. The caller zeroes the object (no ZeroTLAB), so
// needzero only matters with ZeroTLAB, which makes the fast path bail out on needzero spans.
void MMTkBarrierSetAssembler::span_allocate(MacroAssembler* masm, const MMTkSpanFastPathInfo* info, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Label& slow_case) {
  assert(obj == rax, "obj must be in rax");
  const Register scratch = rscratch1;
  assert_different_registers(obj, var_size_in_bytes, t1, scratch);
  const int mutator_offset = in_bytes(JavaThread::third_party_heap_mutator_offset());

  const Register span = scratch;
  const Register bit = t1;

  // Allocate-black: the flags only change while mutators are stopped.
  const bool inline_black = VM_Version::supports_bmi2();
  __ movptr(span, Address(r15_thread, mutator_offset + (int)info->space_offset));
  {
    Label not_black;
    __ cmpb(Address(span, (int)info->allocate_black_offset), 0);
    __ jcc(Assembler::equal, not_black);
    if (inline_black) {
      __ cmpb(Address(span, (int)info->inline_black_offset), 0);
      __ jcc(Assembler::equal, slow_case);
    } else {
      __ jmp(slow_case);
    }
    __ bind(not_black);
  }

  // span = mutator.alloc[span class]
  if (var_size_in_bytes == noreg) {
    int spc = mmtk_span_class_for_size(info, (size_t)con_size_in_bytes);
    if (spc == 0) {
      __ jmp(slow_case);
      return;
    }
    __ movptr(span, Address(r15_thread, mutator_offset + (int)info->spans_offset + spc * wordSize));
  } else {
    // The size is below max_non_los_default_alloc_bytes (checked by the caller) and a multiple
    // of 8.
    assert(MMTkMutatorContext::max_non_los_default_alloc_bytes <= info->max_size + 1, "size class table too small");
    __ movptr(bit, var_size_in_bytes);
    __ shrptr(bit, LogBytesPerLong);
    __ mov64(span, (int64_t)(intptr_t)info->size_to_span_class);
    __ movzbl(bit, Address(span, bit, Address::times_1));
    __ movptr(span, Address(r15_thread, bit, Address::times_8, mutator_offset + (int)info->spans_offset));
  }

  if (ZeroTLAB) {
    // The interpreter does not zero the object with ZeroTLAB.
    __ cmpb(Address(span, (int)info->needzero_offset), 0);
    __ jcc(Assembler::notEqual, slow_case);
  }

  // obj = cache, bit = ctz(cache)
  __ movq(obj, Address(span, (int)info->alloc_cache_offset));
  __ bsfq(bit, obj);
  __ jcc(Assembler::zero, slow_case);
  // obj = cache >> (bit + 1)
  if (VM_Version::supports_bmi2()) {
    __ shrxq(obj, obj, bit);
  } else if (bit == rcx) {
    __ shrq(obj);
  } else {
    __ push(rcx);
    __ movq(rcx, bit);
    __ shrq(obj);
    __ pop(rcx);
  }
  __ shrq(obj, 1);
  // bit = result = freeindex + bit
  __ addq(bit, Address(span, (int)info->freeindex_offset));
  __ cmpq(bit, Address(span, (int)info->nelems_offset));
  __ jcc(Assembler::aboveEqual, slow_case);
  // bit = freeidx = result + 1
  __ addq(bit, 1);
  {
    Label not_at_word_end;
    __ testl(bit, 63);
    __ jcc(Assembler::notZero, not_at_word_end);
    __ cmpq(bit, Address(span, (int)info->nelems_offset));
    __ jcc(Assembler::notEqual, slow_case);
    __ bind(not_at_word_end);
  }
  __ movq(Address(span, (int)info->alloc_cache_offset), obj);
  __ movq(Address(span, (int)info->freeindex_offset), bit);
  __ addq(Address(span, (int)info->alloc_count_offset), 1);
  if (inline_black) {
    // Allocate-black (Go: gcmarknewobject).
    Label not_black;
    __ movptr(obj, Address(r15_thread, mutator_offset + (int)info->space_offset));
    __ cmpb(Address(obj, (int)info->allocate_black_offset), 0);
    __ jcc(Assembler::equal, not_black);
    // obj = index = freeidx - 1, bit = 1 << (index % 64), obj = index / 64
    __ lea(obj, Address(bit, -1));
    __ movl(bit, 1);
    __ shlxq(bit, bit, obj);
    __ shrq(obj, 6);
    {
      // The scan bit first, so that a span scan never sees the object marked but not scanned.
      Label no_scan;
      __ cmpb(Address(span, (int)info->black_scan_offset), 0);
      __ jcc(Assembler::equal, no_scan);
      __ lock();
      __ orq(Address(span, obj, Address::times_8, (int)info->scan_bits_offset), bit);
      __ bind(no_scan);
    }
    __ lock();
    __ orq(Address(span, obj, Address::times_8, (int)info->mark_bits_offset), bit);
    // Only this mutator writes its black_bytes.
    __ movq(bit, Address(span, (int)info->elemsize_offset));
    __ addq(Address(r15_thread, mutator_offset + (int)info->black_bytes_offset), bit);
    // bit = freeidx again
    __ movq(bit, Address(span, (int)info->freeindex_offset));
    __ bind(not_black);
  }
  // obj = start + (freeidx - 1) * elemsize
  __ lea(obj, Address(bit, -1));
  __ imulq(obj, Address(span, (int)info->elemsize_offset));
  __ addq(obj, Address(span, (int)info->start_offset));
}

void MMTkBarrierSetAssembler::weak_reference_load_barrier(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Register tmp1, uintptr_t flag_addr) {
  bool on_oop = type == T_OBJECT || type == T_ARRAY;
  bool on_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool on_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool on_reference = on_weak || on_phantom;

  if (mmtk_enable_reference_load_barrier) {
    if (on_oop && on_reference) {
      Label done;

      assert_different_registers(dst, tmp1);

      // No slow-call if the barrier is not active
      // intptr_t tmp1_q = *flag_addr;
      __ movptr(tmp1, (intptr_t) flag_addr);
      // Load with zero extension to 32 bits.
      // uint32_t tmp1_l = (uint32_t)(*(unt8_t*)tmp1_q);
      __ movzbl(tmp1, Address(tmp1, 0));
      // if (tmp1_l == 0) goto done;
      __ testl(tmp1, tmp1);
      __ jcc(Assembler::zero, done);
      // if (dst == 0) goto done;
      __ testptr(dst, dst);
      __ jcc(Assembler::zero, done);
      // Do slow-call
      __ pusha();
      __ mov(c_rarg0, dst);
      __ MacroAssembler::call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::load_reference_call), 1);
      __ popa();
      __ bind(done);
    }
  }
}

#undef __

//////////////////// Assembler for C1 ////////////////////

// Generate runtime stubs for the "runtime code blobs" in MMTkBarrierSetC1

#define __ sasm->

void MMTkBarrierSetAssembler::generate_c1_runtime_stub_general(StubAssembler* sasm, const char* name, address entry_point, int argc, bool do_code_patch) {
  __ prologue(name, false);
  __ save_live_registers_no_oop_map(true);

  if (argc > 0) __ load_parameter(0, c_rarg0);
  if (argc > 1) __ load_parameter(1, c_rarg1);
  if (argc > 2) __ load_parameter(2, c_rarg2);
  if (argc > 3) {
    guarantee(false, "Too many args");
  }

  if (do_code_patch) {
    // We don't know the field offset when a code patching is required.
    // As a temporary fix, we apply field barrier to all fields in this object.
    __ call_VM_leaf(FN_ADDR(MMTkBarrierSetRuntime::object_probable_write_pre_call), c_rarg0);
  } else {
    __ call_VM_leaf_base(entry_point, argc);
  }

  __ restore_live_registers(true);
  __ epilogue();
}

void MMTkBarrierSetAssembler::generate_c1_load_reference_runtime_stub(StubAssembler* sasm) {
  generate_c1_runtime_stub_general(sasm, "c1_load_reference_runtime_stub", FN_ADDR(MMTkBarrierSetRuntime::load_reference_call), 1);
}

void MMTkBarrierSetAssembler::generate_c1_object_reference_write_pre_runtime_stub(StubAssembler* sasm) {
  generate_c1_runtime_stub_general(sasm, "c1_object_reference_write_pre_stub", FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_pre_call), 3);
}

void MMTkBarrierSetAssembler::generate_c1_object_reference_write_pre_runtime_stub_with_patch_fix(StubAssembler* sasm) {
  generate_c1_runtime_stub_general(sasm, "c1_object_reference_write_pre_stub", FN_ADDR(mmtk_object_reference_write_slow), 3, true);
}

void MMTkBarrierSetAssembler::generate_c1_object_reference_write_post_runtime_stub(StubAssembler* sasm) {
  generate_c1_runtime_stub_general(sasm, "c1_object_reference_write_post_stub", FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_post_call), 3);
}

void MMTkBarrierSetAssembler::generate_c1_object_reference_write_slow_runtime_stub(StubAssembler* sasm) {
  generate_c1_runtime_stub_general(sasm, "c1_object_reference_write_slow_stub", FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), 3);
}

#undef __

// Generate code stubs

#define __ ce->masm()->

void MMTkBarrierSetAssembler::generate_c1_ref_load_barrier_stub_call(LIR_Assembler* ce, MMTkC1ReferenceLoadBarrierStub* stub) {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*)BarrierSet::barrier_set()->barrier_set_c1();

  __ bind(*stub->entry());
  assert(stub->val->is_register(), "Precondition.");

  Register val_reg = stub->val->as_register();

  __ testptr(val_reg, val_reg);
  __ jcc(Assembler::zero, *stub->continuation());
  ce->store_parameter(stub->val->as_register(), 0);
  __ call(RuntimeAddress(bs->load_reference_c1_runtime_code_blob()->code_begin()));
  __ jmp(*stub->continuation());
}

#undef __
