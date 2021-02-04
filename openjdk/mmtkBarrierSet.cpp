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

#include "mmtkBarrierSet.hpp"

#ifdef COMPILER1
#include "mmtkBarrierSetC1.hpp"
#endif
#ifdef COMPILER2
#include "mmtkBarrierSetC2.hpp"
#endif
#include "mmtkBarrierSetAssembler_x86.hpp"

#include "runtime/interfaceSupport.inline.hpp"
#include <cstring>
#include "barriers/mmtkNoBarrier.hpp"

bool MMTkBarrierSet::enable_write_barrier = false;

MMTkBarrierRuntime* MMTkBarrierSet::_runtime = NULL;
MMTkBarrierAssembler* MMTkBarrierSet::_assembler = NULL;
MMTkBarrierC1* MMTkBarrierSet::_c1 = NULL;
MMTkBarrierC2* MMTkBarrierSet::_c2 = NULL;

MMTkBarrierSet::MMTkBarrierSet(MemRegion whole_heap): BarrierSet(
      make_barrier_set_assembler<MMTkBarrierSetAssembler>(),
      make_barrier_set_c1<MMTkBarrierSetC1>(),
      make_barrier_set_c2<MMTkBarrierSetC2>(),
      BarrierSet::FakeRtti(BarrierSet::ThirdPartyHeapBarrierSet)
    )
    , _whole_heap(whole_heap) {
    char* plan = std::getenv("MMTK_PLAN");
    if (std::strcmp(plan, "NoGC")) {
        _runtime = new MMTkNoBarrierRuntime();
        _assembler = new MMTkNoBarrierAssembler();
        _c1 = new MMTkNoBarrierC1();
        _c2 = new MMTkNoBarrierC2();
    } else if (std::strcmp(plan, "SemiSpace")) {
        _runtime = new MMTkNoBarrierRuntime();
        _assembler = new MMTkNoBarrierAssembler();
        _c1 = new MMTkNoBarrierC1();
        _c2 = new MMTkNoBarrierC2();
    } else {
        guarantee(false, "Unimplemented");
    }
}

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
    return call == CAST_FROM_FN_PTR(address, MMTkBarrierSet::record_modified_node)
        || call == CAST_FROM_FN_PTR(address, MMTkBarrierSet::record_modified_edge);
}

void MMTkBarrierSet::record_modified_node(void* obj) {
    _runtime->record_modified_node(obj);
}

void MMTkBarrierSet::record_modified_edge(void* slot) {
    _runtime->record_modified_edge(slot);
}