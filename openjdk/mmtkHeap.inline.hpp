/*
 * Copyright (c) 2006, 2015, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_GC_MMTK_MMTKHEAP_INLINE_HPP
#define SHARE_VM_GC_MMTK_MMTKHEAP_INLINE_HPP

#include "mmtkHeap.hpp"
#include "mmtkMutator.hpp"

HeapWord* MMTkHeap::mem_allocate(size_t size, bool* gc_overhead_limit_was_exceeded) {
    MMTkMutatorContext* thread = (MMTkMutatorContext*) (void*) Thread::current()->third_party_heap_mutator;
    HeapWord* obj = thread->alloc(size << LogHeapWordSize);
   //  post_alloc(Thread::current()->mmtk_mutator(), obj_ptr, NULL, size << LogHeapWordSize, 0);
    // guarantee(obj, "MMTk gave us null!");
    return obj;
}

#endif // SHARE_VM_GC_MMTK_MMTKHEAP_INLINE_HPP
