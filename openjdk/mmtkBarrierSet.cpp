/*
 * Copyright (c) 2000, 2017, Oracle and/or its affiliates. All rights reserved.
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
#include "barriers/mmtkNoBarrier.hpp"
#include "barriers/mmtkObjectBarrier.hpp"
#include "mmtkBarrierSet.hpp"
#include "mmtkBarrierSetAssembler_x86.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#ifdef COMPILER1
#include "mmtkBarrierSetC1.hpp"
#endif
#ifdef COMPILER2
#include "mmtkBarrierSetC2.hpp"
#endif

MMTkAllocatorOffsets get_tlab_top_and_end_offsets(AllocatorSelector selector) {
  int tlab_top_offset, tlab_end_offset;
  int allocators_base_offset = in_bytes(JavaThread::third_party_heap_mutator_offset())
    + in_bytes(byte_offset_of(MMTkMutatorContext, allocators));

  if (selector.tag == TAG_IMMIX) {
    int allocator_base_offset = allocators_base_offset
      + in_bytes(byte_offset_of(Allocators, immix))
      + selector.index * sizeof(ImmixAllocator);
    tlab_top_offset = allocator_base_offset + in_bytes(byte_offset_of(ImmixAllocator, cursor));
    tlab_end_offset = allocator_base_offset + in_bytes(byte_offset_of(ImmixAllocator, limit));
  } else if (selector.tag == TAG_BUMP_POINTER) {
    int allocator_base_offset = allocators_base_offset
      + in_bytes(byte_offset_of(Allocators, bump_pointer))
      + selector.index * sizeof(BumpAllocator);
    tlab_top_offset = allocator_base_offset + in_bytes(byte_offset_of(BumpAllocator, cursor));
    tlab_end_offset = allocator_base_offset + in_bytes(byte_offset_of(BumpAllocator, limit));
  } else if (selector.tag == TAG_MARK_COMPACT) {
    int allocator_base_offset = allocators_base_offset
      + in_bytes(byte_offset_of(Allocators, markcompact))
      + selector.index * sizeof(MarkCompactAllocator)
      + in_bytes(byte_offset_of(MarkCompactAllocator, bump_allocator));
    tlab_top_offset = allocator_base_offset + in_bytes(byte_offset_of(BumpAllocator, cursor));
    tlab_end_offset = allocator_base_offset + in_bytes(byte_offset_of(BumpAllocator, limit));
  } else {
    fatal("Unimplemented allocator fastpath\n");
    // Setting values to make compiler happy about unitialized variables. This
    // case should never be reached in practice, however.
    tlab_top_offset = 0;
    tlab_end_offset = 0;
  }

  MMTkAllocatorOffsets alloc_offsets;
  alloc_offsets.tlab_top_offset = tlab_top_offset;
  alloc_offsets.tlab_end_offset = tlab_end_offset;
  return alloc_offsets;
}

MMTkBarrierBase* get_selected_barrier() {
  static MMTkBarrierBase* selected_barrier = NULL;
  if (selected_barrier) return selected_barrier;
  const char* barrier = mmtk_active_barrier();
  if (strcmp(barrier, "NoBarrier") == 0) selected_barrier = new MMTkNoBarrier();
  else if (strcmp(barrier, "ObjectBarrier") == 0) selected_barrier = new MMTkObjectBarrier();
  else guarantee(false, "Unimplemented");
  return selected_barrier;
}

MMTkBarrierSet::MMTkBarrierSet(MemRegion whole_heap):
  BarrierSet(get_selected_barrier()->create_assembler(),
             get_selected_barrier()->create_c1(),
             get_selected_barrier()->create_c2(),
             BarrierSet::FakeRtti(BarrierSet::ThirdPartyHeapBarrierSet)),
  _whole_heap(whole_heap),
  _runtime(get_selected_barrier()->create_runtime()) {}

void MMTkBarrierSet::write_ref_array_work(MemRegion mr) {
  guarantee(false, "NoBarrier::write_ref_arrey_work not supported");
}

// Inform the BarrierSet that the the covered heap region that starts
// with "base" has been changed to have the given size (possibly from 0,
// for initialization.)
void MMTkBarrierSet::resize_covered_region(MemRegion new_region) {
  guarantee(false, "NoBarrier::resize_covered_region not supported");
}


void MMTkBarrierSet::on_thread_destroy(Thread* thread) {
  thread->third_party_heap_mutator.flush();
}

void MMTkBarrierSet::on_thread_attach(JavaThread* thread) {
  thread->third_party_heap_mutator.flush();
}

void MMTkBarrierSet::on_thread_detach(JavaThread* thread) {
  thread->third_party_heap_mutator.flush();
}


// If the barrier set imposes any alignment restrictions on boundaries
// within the heap, this function tells whether they are met.
bool MMTkBarrierSet::is_aligned(HeapWord* addr) {
  return true;
}

// Print a description of the memory for the barrier set
void MMTkBarrierSet::print_on(outputStream* st) const {

}

bool MMTkBarrierSet::is_slow_path_call(address call) {
  return runtime()->is_slow_path_call(call);
}

void MMTkBarrierSetRuntime::object_reference_write_pre_call(void* src, void* slot, void* target) {
  ::mmtk_object_reference_write_pre((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, src, slot, target);
}

void MMTkBarrierSetRuntime::object_reference_write_post_call(void* src, void* slot, void* target) {
  ::mmtk_object_reference_write_post((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, src, slot, target);
}

void MMTkBarrierSetRuntime::object_reference_write_slow_call(void* src, void* slot, void* target) {
  ::mmtk_object_reference_write_slow((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, src, slot, target);
}

void MMTkBarrierSetRuntime::object_reference_array_copy_pre_call(void* src, void* dst, size_t count) {
  ::mmtk_array_copy_pre((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, src, dst, count);
}

void MMTkBarrierSetRuntime::object_reference_array_copy_post_call(void* src, void* dst, size_t count) {
  ::mmtk_array_copy_post((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, src, dst, count);
}
