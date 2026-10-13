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

#ifndef MMTK_OPENJDK_MMTK_BARRIER_SET_HPP
#define MMTK_OPENJDK_MMTK_BARRIER_SET_HPP

#include "gc/shared/accessBarrierSupport.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetConfig.hpp"
#include "memory/memRegion.hpp"
#include "mmtk.h"
#include "oops/access.hpp"
#include "oops/arrayOop.hpp"
#include "oops/accessBackend.hpp"
#include "oops/compressedOops.hpp"
#include "oops/oopsHierarchy.hpp"
#include "utilities/fakeRttiSupport.hpp"
#include "utilities/macros.hpp"
#include CPU_HEADER(mmtkBarrierSetAssembler)

extern bool mmtk_enable_allocation_fastpath;
extern bool mmtk_enable_barrier_fastpath;
extern bool mmtk_enable_reference_load_barrier;
extern bool mmtk_enable_span_allocation_fastpath;

inline intptr_t field_unlog_bits_base_address() {
  return static_cast<intptr_t>(get_global_side_metadata_vm_base_address());
}

inline intptr_t field_unlog_bits_base_address_compressed() {
  return static_cast<intptr_t>(get_global_side_metadata_vm_base_address());
}

inline intptr_t vo_bit_base_address() {
  return static_cast<intptr_t>(get_vo_bit_address());
}

struct MMTkAllocatorOffsets {
  int tlab_top_offset;
  int tlab_end_offset;
};

/**
 * Return the offset (from the start of the mutator) for the TLAB top (cursor)
 * and end (limit) for an MMTk Allocator.
 *
 * @param selector The current MMTk Allocator being used
 * @return the offsets to the top and end of the TLAB
 */
MMTkAllocatorOffsets get_tlab_top_and_end_offsets(AllocatorSelector selector);

/**
 * The SpanAllocator inline fast path (Go's nextFreeFast) for the default allocator, or NULL if
 * the default allocator is not a SpanAllocator or the fast path is disabled
 * (MMTK_ENABLE_SPAN_ALLOCATION_FASTPATH=false). Offsets named `*_offset` relative to the mutator
 * are relative to the start of the MMTk mutator, not to the thread.
 */
const MMTkSpanFastPathInfo* mmtk_span_fastpath();

/// The span class index for a constant allocation size (0 if none: slot 0 always holds the empty
/// span, so the fast path fails on it).
inline uint8_t mmtk_span_class_for_size(const MMTkSpanFastPathInfo* info, size_t size) {
  if (size > info->max_size || (size & 7) != 0) return 0;
  return info->size_to_span_class[size >> 3];
}

#define FN_ADDR(function) CAST_FROM_FN_PTR(address, function)

class MMTkBarrierSetRuntime: public CHeapObj<mtGC> {
public:
  /// Weak ref load barrier
  static void load_reference_call(void* ref);
  /// Generic pre-write barrier. Called by fast-paths.
  static void object_reference_write_pre_call(void* src, void* slot, void* target);
  /// Generic post-write barrier. Called by fast-paths.
  static void object_reference_write_post_call(void* src, void* slot, void* target);
  /// Generic slow-path. Called by fast-paths.
  static void object_reference_write_slow_call(void* src, void* slot, void* target);
  /// Generic arraycopy pre-barrier. Called by fast-paths.
  static void object_reference_array_copy_pre_call(void* src, void* dst, size_t count);
  /// Generic arraycopy post-barrier. Called by fast-paths.
  static void object_reference_array_copy_post_call(void* src, void* dst, size_t count);
  /// A pre-barrier indicating some fields will be modified soon. Called by fast-paths.
  static void object_probable_write_pre_call(void* obj);
  /// Check if the address is a slow-path function.
  virtual bool is_slow_path_call(address call) const {
    return call == CAST_FROM_FN_PTR(address, object_reference_write_pre_call)
        || call == CAST_FROM_FN_PTR(address, object_reference_write_post_call)
        || call == CAST_FROM_FN_PTR(address, object_reference_write_slow_call)
        || call == CAST_FROM_FN_PTR(address, object_reference_array_copy_pre_call)
        || call == CAST_FROM_FN_PTR(address, object_reference_array_copy_post_call)
        || call == CAST_FROM_FN_PTR(address, load_reference_call)
        || call == CAST_FROM_FN_PTR(address, object_probable_write_pre_call);
  }

  /// Full pre-barrier
  virtual void object_reference_write_pre(oop src, oop* slot, oop target) const {};
  /// Full post-barrier
  virtual void object_reference_write_post(oop src, oop* slot, oop target) const {};
  /// Full arraycopy pre-barrier
  virtual void object_reference_array_copy_pre(oop* src, oop* dst, size_t count) const {};
  /// Full arraycopy pre-barrier, when the destination is not initialised yet (IS_DEST_UNINITIALIZED).
  /// The old contents of `dst` must not be read.  By default, the same as `object_reference_array_copy_pre`.
  virtual void object_reference_array_copy_pre_dst_uninitialized(oop* src, oop* dst, size_t count) const {
    object_reference_array_copy_pre(src, dst, count);
  };
  /// Full arraycopy post-barrier
  virtual void object_reference_array_copy_post(oop* src, oop* dst, size_t count) const {};
  /// Called before `src` is cloned into the newly allocated `dst` (`Object.clone`).
  virtual void object_clone_pre(oop src, oop dst) const {};
  /// Called before `value` is stored into a location outside the heap (a VM root, e.g. an
  /// OopStorage handle), through `oop_store_not_in_heap` and the not-in-heap atomics.
  virtual void object_reference_write_not_in_heap(oop value) const {};
  /// java.lang.Reference load barrier
  virtual void load_reference(DecoratorSet decorators, oop value) const {};
  /// Called at the end of every C2 slowpath allocation.
  /// Deoptimization can happen after C2 slowpath allocation, and the newly allocated object can be promoted.
  /// So this callback is requierd for any generational collectors.
  virtual void object_probable_write(oop new_obj) const {};
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
  virtual MMTkBarrierSetAssembler* create_assembler() const { return NOT_ZERO(new Assembler()) ZERO_ONLY(NULL); }
  virtual MMTkBarrierSetC1* create_c1() const { return COMPILER1_PRESENT(new C1()) NOT_COMPILER1(NULL); }
  virtual MMTkBarrierSetC2* create_c2() const { return COMPILER2_PRESENT(new C2()) NOT_COMPILER2(NULL); }
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
  virtual void write_ref_array_work(MemRegion mr);

public:
  MMTkBarrierSet(MemRegion whole_heap);

  inline static MMTkBarrierSetRuntime* runtime() {
    return ((MMTkBarrierSet*) BarrierSet::barrier_set())->_runtime;
  }

  virtual void on_thread_destroy(Thread* thread) override;
  virtual void on_thread_attach(Thread* thread) override;
  virtual void on_thread_detach(Thread* thread) override;

  virtual void on_slowpath_allocation_exit(JavaThread* thread, oop new_obj) override {
    runtime()->object_probable_write(new_obj);
  }

  // Inform the BarrierSet that the the covered heap region that starts
  // with "base" has been changed to have the given size (possibly from 0,
  // for initialization.)
  virtual void resize_covered_region(MemRegion new_region);

  // If the barrier set imposes any alignment restrictions on boundaries
  // within the heap, this function tells whether they are met.
  virtual bool is_aligned(HeapWord* addr);

  // Print a description of the memory for the barrier set
  virtual void print_on(outputStream* st) const override;


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
    // Needed for weak references
    static oop oop_load_in_heap_at(oop base, ptrdiff_t offset) {
      oop value = Raw::oop_load_in_heap_at(base, offset);
      const bool on_strong_oop_ref = (decorators & ON_STRONG_OOP_REF) != 0;
      const bool peek              = (decorators & AS_NO_KEEPALIVE) != 0;
      const bool needs_enqueue     = (!peek && !on_strong_oop_ref);
      if (needs_enqueue && value != NULL) {
        runtime()->load_reference(decorators, value);
      }
      return value;
    }

    template <typename T>
    static oop oop_load_not_in_heap(T* addr) {
      oop value = Raw::template oop_load<oop>(addr);
      const bool on_strong_oop_ref = (decorators & ON_STRONG_OOP_REF) != 0;
      const bool peek              = (decorators & AS_NO_KEEPALIVE) != 0;
      const bool needs_enqueue     = (!peek && !on_strong_oop_ref);
      if (needs_enqueue && value != NULL) {
        runtime()->load_reference(decorators, value);
      }
      return value;
    }

    // Stores into VM roots (off-heap).  Barriers that need it see the new value first.
    template <typename T>
    static void oop_store_not_in_heap(T* addr, oop value) {
      runtime()->object_reference_write_not_in_heap(value);
      Raw::oop_store(addr, value);
    }

    template <typename T>
    static oop oop_atomic_cmpxchg_not_in_heap(T* addr, oop compare_value, oop new_value) {
      runtime()->object_reference_write_not_in_heap(new_value);
      return Raw::oop_atomic_cmpxchg(addr, compare_value, new_value);
    }

    template <typename T>
    static oop oop_atomic_xchg_not_in_heap(T* addr, oop new_value) {
      runtime()->object_reference_write_not_in_heap(new_value);
      return Raw::oop_atomic_xchg(addr, new_value);
    }

    // Defensive: will catch weak oops at addresses in heap
    template <typename T>
    static oop oop_load_in_heap(T* addr) {
      UNREACHABLE();
      return NULL;
    }

    template <typename T>
    static void oop_store_in_heap(T* addr, oop value) {
      UNREACHABLE();
    }

    static void oop_store_in_heap_at(oop base, ptrdiff_t offset, oop value) {
      runtime()->object_reference_write_pre(base, (oop*) (size_t((void*) base) + offset), value);
      Raw::oop_store_at(base, offset, value);
      runtime()->object_reference_write_post(base, (oop*) (size_t((void*) base) + offset), value);
    }

    template <typename T>
    static oop oop_atomic_cmpxchg_in_heap(T* addr, oop compare_value, oop new_value) {
      UNREACHABLE();
      return NULL;
    }

    static oop oop_atomic_cmpxchg_in_heap_at(oop base, ptrdiff_t offset, oop compare_value, oop new_value) {
      runtime()->object_reference_write_pre(base, (oop*) (size_t((void*) base) + offset), new_value);
      oop result = Raw::oop_atomic_cmpxchg_at(base, offset, compare_value, new_value);
      runtime()->object_reference_write_post(base, (oop*) (size_t((void*) base) + offset), new_value);
      return result;
    }

    template <typename T>
    static oop oop_atomic_xchg_in_heap(T* addr, oop new_value) {
      UNREACHABLE();
      return NULL;
    }

    static oop oop_atomic_xchg_in_heap_at(oop base, ptrdiff_t offset, oop new_value) {
      runtime()->object_reference_write_pre(base, (oop*) (size_t((void*) base) + offset), new_value);
      oop result = Raw::oop_atomic_xchg_at(base, offset, new_value);
      runtime()->object_reference_write_post(base, (oop*) (size_t((void*) base) + offset), new_value);
      return result;
    }

    template <typename T>
    static bool oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                                      arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                                      size_t length) {
      T* src = arrayOopDesc::obj_offset_to_raw(src_obj, src_offset_in_bytes, src_raw);
      T* dst = arrayOopDesc::obj_offset_to_raw(dst_obj, dst_offset_in_bytes, dst_raw);
      if ((decorators & IS_DEST_UNINITIALIZED) != 0) {
        runtime()->object_reference_array_copy_pre_dst_uninitialized((oop*) src, (oop*) dst, length);
      } else {
        runtime()->object_reference_array_copy_pre((oop*) src, (oop*) dst, length);
      }
      bool result = Raw::oop_arraycopy_in_heap(src_obj, src_offset_in_bytes, src_raw,
                                       dst_obj, dst_offset_in_bytes, dst_raw,
                                       length);
      runtime()->object_reference_array_copy_post((oop*) src, (oop*) dst, length);
      return result;
    }

    static void clone_in_heap(oop src, oop dst, size_t size) {
      runtime()->object_clone_pre(src, dst);
      Raw::clone_in_heap(src, dst, size);
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


#endif // MMTK_OPENJDK_MMTK_BARRIER_SET_HPP
