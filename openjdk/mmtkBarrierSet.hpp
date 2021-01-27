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

#ifndef SHARE_VM_GC_MMTK_NOBARRIER_HPP
#define SHARE_VM_GC_MMTK_NOBARRIER_HPP

#include "gc/shared/accessBarrierSupport.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetConfig.hpp"
#include "memory/memRegion.hpp"
#include "oops/access.hpp"
#include "oops/accessBackend.hpp"
#include "oops/oopsHierarchy.hpp"
#include "utilities/fakeRttiSupport.hpp"
#include "mmtk.h"

#define MMTK_ENABLE_ALLOCATION_FASTPATH true

#define BARRIER_NORMAL 0
#define BARRIER_TEST_GLOBAL 1
#define BARRIER_SINGLE_PAGE_METADATA 2
#define BARRIER_VARIENT BARRIER_NORMAL

#define NORMAL_BARRIER_NO_SLOWPATH false


#if MMTK_GC_GENCOPY || MMTK_GC_NOGC
#define MMTK_ENABLE_WRITE_BARRIER true
#define MMTK_ENABLE_WRITE_BARRIER_FASTPATH true
#else
#define MMTK_ENABLE_WRITE_BARRIER false
#define MMTK_ENABLE_WRITE_BARRIER_FASTPATH false
#endif

#define MMTK_HEAP_END 0x0000200000000000ULL
#define MMTK_LOG_CHUNK_SIZE 22
#define MMTK_LOG_PER_CHUNK_METADATA_SIZE (12 + 4)
#define MMTK_CHUNK_MASK ((1ULL << MMTK_LOG_CHUNK_SIZE) - 1)
#define MMTK_NURSERY_START ((void*) 0x20000000000ULL)
#define MMTK_NURSERY_END   ((void*) 0x40000000000ULL)

// This class provides the interface between a barrier implementation and
// the rest of the system.


struct MMTkBarrierRuntime: AllStatic {
public:
  static void record_modified_node(void* src);
  inline static void record_modified_node_fast(void* obj) {
#if MMTK_ENABLE_WRITE_BARRIER_FASTPATH

#if BARRIER_VARIENT == BARRIER_TEST_GLOBAL
    const char* byte = (char*) (MMTK_HEAP_END);
    if (((*byte) & 1) != 0) {
      record_modified_node(obj);
    }
#elif BARRIER_VARIENT == BARRIER_SINGLE_PAGE_METADATA
    obj = (void*) (((size_t) obj) & ((1 << 22) - 1));
    const size_t chunk_index = ((size_t) obj) >> MMTK_LOG_CHUNK_SIZE;
    const size_t chunk_offset = chunk_index << MMTK_LOG_PER_CHUNK_METADATA_SIZE;
    const size_t bit_index = (((size_t) obj) & MMTK_CHUNK_MASK) >> 3;
    const size_t word_offset = ((bit_index >> 6) << 3);
    const size_t* word = (size_t*) (MMTK_HEAP_END + chunk_offset + word_offset);
    const size_t bit_offset = bit_index & 63;
    if (((*word) & (1ULL << bit_offset)) != 0) {
      record_modified_node(obj);
    }
#else
    const size_t chunk_index = ((size_t) obj) >> MMTK_LOG_CHUNK_SIZE;
    const size_t chunk_offset = chunk_index << MMTK_LOG_PER_CHUNK_METADATA_SIZE;
    const size_t bit_index = (((size_t) obj) & MMTK_CHUNK_MASK) >> 3;
    const size_t word_offset = ((bit_index >> 6) << 3);
    const size_t* word = (size_t*) (MMTK_HEAP_END + chunk_offset + word_offset);
    const size_t bit_offset = bit_index & 63;
  #if NORMAL_BARRIER_NO_SLOWPATH
    if (((*word) & (1ULL << bit_offset)) != 0) {
  #else
    if (((*word) & (1ULL << bit_offset)) == 0) {
  #endif
      record_modified_node(obj);
    }
#endif

#else
    record_modified_node(obj);
#endif
  }
  static void record_modified_edge(void* slot);
};

class MMTkBarrierSet : public BarrierSet {
  friend class VMStructs;
private:
  MemRegion _whole_heap;

protected:
  virtual void write_ref_array_work(MemRegion mr) ;

public:
  MMTkBarrierSet(MemRegion whole_heap);

  virtual void on_thread_destroy(Thread* thread);
  virtual void on_thread_attach(JavaThread* thread);
  virtual void on_thread_detach(JavaThread* thread);

  // Inform the BarrierSet that the the covered heap region that starts
  // with "base" has been changed to have the given size (possibly from 0,
  // for initialization.)
  virtual void resize_covered_region(MemRegion new_region);

  // If the barrier set imposes any alignment restrictions on boundaries
  // within the heap, this function tells whether they are met.
  virtual bool is_aligned(HeapWord* addr);

  // Print a description of the memory for the barrier set
  virtual void print_on(outputStream* st) const;


  // The AccessBarrier of a BarrierSet subclass is called by the Access API
  // (cf. oops/access.hpp) to perform decorated accesses. GC implementations
  // may override these default access operations by declaring an
  // AccessBarrier class in its BarrierSet. Its accessors will then be
  // automatically resolved at runtime.
  //
  // In order to register a new FooBarrierSet::AccessBarrier with the Access API,
  // the following steps should be taken:
  // 1) Provide an enum "name" for the BarrierSet in barrierSetConfig.hpp
  // 2) Make sure the barrier set headers are included from barrierSetConfig.inline.hpp
  // 3) Provide specializations for BarrierSet::GetName and BarrierSet::GetType.
  template <DecoratorSet decorators, typename BarrierSetT = MMTkBarrierSet>
  class AccessBarrier: public BarrierSet::AccessBarrier<decorators, BarrierSetT> {
  private:
    typedef BarrierSet::AccessBarrier<decorators, BarrierSetT> Raw;
  public:
    template <typename T>
    static void oop_store_in_heap(T* addr, oop value) {
      guarantee(false, "unreachable");
    }

    static void oop_store_in_heap_at(oop base, ptrdiff_t offset, oop value) {
      Raw::oop_store_at(base, offset, value);
#if MMTK_ENABLE_WRITE_BARRIER
      MMTkBarrierRuntime::record_modified_node_fast((void*) base);
#endif
    }

    template <typename T>
    static oop oop_atomic_cmpxchg_in_heap(oop new_value, T* addr, oop compare_value) {
      guarantee(false, "unreachable");
      return NULL;
    }

    static oop oop_atomic_cmpxchg_in_heap_at(oop new_value, oop base, ptrdiff_t offset, oop compare_value) {
      oop result = Raw::oop_atomic_cmpxchg_at(new_value, base, offset, compare_value);
#if MMTK_ENABLE_WRITE_BARRIER
      MMTkBarrierRuntime::record_modified_node_fast((void*) base);
#endif
      return result;
    }

    template <typename T>
    static oop oop_atomic_xchg_in_heap(oop new_value, T* addr) {
      guarantee(false, "unreachable");
      return NULL;
    }

    static oop oop_atomic_xchg_in_heap_at(oop new_value, oop base, ptrdiff_t offset) {
      oop result = Raw::oop_atomic_xchg_at(new_value, base, offset);
#if MMTK_ENABLE_WRITE_BARRIER
      MMTkBarrierRuntime::record_modified_node_fast((void*) base);
#endif
      return result;
    }

    template <typename T>
    static bool oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                                      arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                                      size_t length) {
      bool result = Raw::oop_arraycopy(src_obj, src_offset_in_bytes, src_raw,
                                dst_obj, dst_offset_in_bytes, dst_raw,
                                length);
#if MMTK_ENABLE_WRITE_BARRIER
      MMTkBarrierRuntime::record_modified_node_fast((void*) dst_obj);
#endif
      return result;
    }

    static void clone_in_heap(oop src, oop dst, size_t size) {
      Raw::clone(src, dst, size);
#if MMTK_ENABLE_WRITE_BARRIER
      MMTkBarrierRuntime::record_modified_node_fast((void*) dst);
#endif
    }
  };

  static bool is_slow_path_call(address call);
};

template<>
struct BarrierSet::GetName<MMTkBarrierSet> {
  static const BarrierSet::Name value = BarrierSet::ThirdPartyHeapBarrierSet;
};

template<>
struct BarrierSet::GetType<BarrierSet::ThirdPartyHeapBarrierSet> {
  typedef ::MMTkBarrierSet type;
};


#endif // SHARE_VM_GC_MMTK_NOBARRIER_HPP