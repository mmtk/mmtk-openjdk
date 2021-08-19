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
#include "mmtkBarrierSetAssembler_x86.hpp"

#define MMTK_ENABLE_ALLOCATION_FASTPATH true
#define MMTK_ENABLE_BARRIER_FASTPATH true

const intptr_t SIDE_METADATA_BASE_ADDRESS = (intptr_t) GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS;

class MMTkBarrierSetRuntime: public CHeapObj<mtGC> {
public:
  virtual void record_modified_node(oop src, ptrdiff_t offset, oop val) {};
  virtual bool is_slow_path_call(address call) {
    return false;
  }
};

class MMTkBarrierC1;
class MMTkBarrierSetC1;
class MMTkBarrierC2;
class MMTkBarrierSetC2;

struct MMTkBarrierBase: public CHeapObj<mtGC> {
  virtual MMTkBarrierSetRuntime* create_runtime() const = 0;
  virtual MMTkBarrierSetAssembler* create_assembler() const = 0;
  virtual MMTkBarrierSetC1* create_c1() const = 0;
  virtual MMTkBarrierSetC2* create_c2() const = 0;
};

template <class Runtime, class Assembler, class C1, class C2>
struct MMTkBarrierImpl: MMTkBarrierBase {
  virtual MMTkBarrierSetRuntime* create_runtime() const { return new Runtime(); }
  virtual MMTkBarrierSetAssembler* create_assembler() const { return new Assembler(); }
  virtual MMTkBarrierSetC1* create_c1() const { return new C1(); }
  virtual MMTkBarrierSetC2* create_c2() const { return new C2(); }
};

// This class provides the interface between a barrier implementation and
// the rest of the system.
class MMTkBarrierSet : public BarrierSet {
  friend class VMStructs;
  friend class MMTkBarrierSetAssembler;
  friend class MMTkBarrierSetC1;
  friend class MMTkBarrierSetC2;

  MemRegion _whole_heap;
  MMTkBarrierSetRuntime* _runtime;

protected:
  virtual void write_ref_array_work(MemRegion mr) ;

public:
  MMTkBarrierSet(MemRegion whole_heap);

  inline static MMTkBarrierSetRuntime* runtime() {
    return ((MMTkBarrierSet*) BarrierSet::barrier_set())->_runtime;
  }

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

  #define UNREACHABLE() guarantee(false, "UNREACHABLE");
  template <DecoratorSet decorators, typename BarrierSetT = MMTkBarrierSet>
  class AccessBarrier: public BarrierSet::AccessBarrier<decorators, BarrierSetT> {
  private:
    typedef BarrierSet::AccessBarrier<decorators, BarrierSetT> Raw;
  public:
    template <typename T>
    static void oop_store_in_heap(T* addr, oop value) {
      UNREACHABLE();
    }

    static void oop_store_in_heap_at(oop base, ptrdiff_t offset, oop value) {
      runtime()->record_modified_node(base, offset, value);
      Raw::oop_store_at(base, offset, value);
    }

    template <typename T>
    static oop oop_atomic_cmpxchg_in_heap(oop new_value, T* addr, oop compare_value) {
      UNREACHABLE();
      return NULL;
    }

    static oop oop_atomic_cmpxchg_in_heap_at(oop new_value, oop base, ptrdiff_t offset, oop compare_value) {
      runtime()->record_modified_node(base, offset, new_value);
      oop result = Raw::oop_atomic_cmpxchg_at(new_value, base, offset, compare_value);
      return result;
    }

    template <typename T>
    static oop oop_atomic_xchg_in_heap(oop new_value, T* addr) {
      UNREACHABLE();
      return NULL;
    }

    static oop oop_atomic_xchg_in_heap_at(oop new_value, oop base, ptrdiff_t offset) {
      runtime()->record_modified_node(base, offset, new_value);
      oop result = Raw::oop_atomic_xchg_at(new_value, base, offset);
      return result;
    }

    template <typename T>
    static bool oop_arraycopy_in_heap_impl(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                                      arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                                      size_t length) {
      T* src = arrayOopDesc::obj_offset_to_raw(src_obj, src_offset_in_bytes, src_raw);
      T* dst = arrayOopDesc::obj_offset_to_raw(dst_obj, dst_offset_in_bytes, dst_raw);
      ::mmtk_object_reference_arraycopy((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) src, (void*) dst, length);
      bool result = Raw::oop_arraycopy(src_obj, src_offset_in_bytes, src_raw,
                                dst_obj, dst_offset_in_bytes, dst_raw,
                                length);
      return result;
    }


    static bool oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, oop* src_raw,
                                      arrayOop dst_obj, size_t dst_offset_in_bytes, oop* dst_raw,
                                      size_t length) {
      return oop_arraycopy_in_heap_impl(src_obj, src_offset_in_bytes, src_raw, dst_obj, dst_offset_in_bytes, dst_raw, length);
    }
    static bool oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, arrayOop* src_raw,
                                      arrayOop dst_obj, size_t dst_offset_in_bytes, arrayOop* dst_raw,
                                      size_t length) {
      return oop_arraycopy_in_heap_impl(src_obj, src_offset_in_bytes, src_raw, dst_obj, dst_offset_in_bytes, dst_raw, length);
    }
    static bool oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, instanceOop* src_raw,
                                      arrayOop dst_obj, size_t dst_offset_in_bytes, instanceOop* dst_raw,
                                      size_t length) {
      return oop_arraycopy_in_heap_impl(src_obj, src_offset_in_bytes, src_raw, dst_obj, dst_offset_in_bytes, dst_raw, length);
    }
    static bool oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, objArrayOop* src_raw,
                                      arrayOop dst_obj, size_t dst_offset_in_bytes, objArrayOop* dst_raw,
                                      size_t length) {
      return oop_arraycopy_in_heap_impl(src_obj, src_offset_in_bytes, src_raw, dst_obj, dst_offset_in_bytes, dst_raw, length);
    }
    static bool oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, typeArrayOop* src_raw,
                                      arrayOop dst_obj, size_t dst_offset_in_bytes, typeArrayOop* dst_raw,
                                      size_t length) {
      return oop_arraycopy_in_heap_impl(src_obj, src_offset_in_bytes, src_raw, dst_obj, dst_offset_in_bytes, dst_raw, length);
    }

    template <typename T>
    static bool oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                                      arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                                      size_t length) {
      return Raw::oop_arraycopy(src_obj, src_offset_in_bytes, src_raw,
                                dst_obj, dst_offset_in_bytes, dst_raw,
                                length);
    }

    static void clone_in_heap(oop src, oop dst, size_t size) {
      Raw::clone(src, dst, size);
      ::mmtk_object_reference_clone((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) src, (void*) dst, size);
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