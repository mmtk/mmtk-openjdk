/*
 * Copyright (c) 2001, 2017, Oracle and/or its affiliates. All rights reserved.
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

#ifndef MMTK_OPENJDK_MMTK_HEAP_HPP
#define MMTK_OPENJDK_MMTK_HEAP_HPP

#include "mmtkBarrierSet.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/gcPolicyCounters.hpp"
#include "gc/shared/gcWhen.hpp"
#include "gc/shared/oopStorage.hpp"
#include "gc/shared/oopStorageParState.hpp"
#include "gc/shared/oopStorageSetParState.hpp"
#include "gc/shared/space.hpp"
#include "gc/shared/strongRootsScope.hpp"
#include "gc/shared/workerThread.hpp"
#include "gc/shared/softRefPolicy.hpp"
#include "memory/iterator.hpp"
#include "memory/metaspace.hpp"
#include "mmtkFinalizerThread.hpp"
#include "mmtkMemoryPool.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/ostream.hpp"

#define WORKER_STACK_SIZE (64 * 1024 * 1024)

template <class T>
struct MaybeUninit {
  MaybeUninit() {}
  T* operator->() {
    return (T*) &_data;
  }
  T& operator*() {
    return *((T*) &_data);
  }
  template<class... Args>
  void init(Args... args) {
    new (&_data) T(args...);
  }
  template<class... Args>
  void reinit(Args... args) {
    ((T*) &_data)->~T();
    new (&_data) T(args...);
  }
private:
  char _data[sizeof(T)];
};

class GCMemoryManager;
class MemoryPool;
//class mmtkGCTaskManager;
class MMTkVMCompanionThread;
class MMTkHeap : public CollectedHeap {
  MMTkMemoryPool* _mmtk_pool;
  GCMemoryManager* _mmtk_manager;
  size_t _n_workers;
  Monitor* _gc_lock;
  ContiguousSpace* _space;
  int _num_root_scan_tasks;
  MMTkVMCompanionThread* _companion_thread;
  WorkerThreads* _workers;
  SoftRefPolicy _soft_ref_policy;
public:
  AllocatorSelector default_allocator_selector;

public:
  jlong _last_gc_time;

private:
  static MMTkHeap* _heap;

public:
  MMTkHeap();

  WorkerThreads* workers() const { return _workers; }

  void schedule_finalizer();

  void set_is_gc_active(bool is_gc_active) {
    _is_stw_gc_active = is_gc_active;
  }

  inline static MMTkHeap* heap() {
    return _heap;
  }

  static HeapWord* allocate_from_tlab(Klass* klass, Thread* thread, size_t size);

  virtual jint initialize() override;
  virtual void enable_collection() override;

  virtual HeapWord* mem_allocate(size_t size, bool* gc_overhead_limit_was_exceeded) override;
  HeapWord* mem_allocate_nonmove(size_t size, bool* gc_overhead_limit_was_exceeded);

  MMTkVMCompanionThread* companion_thread() const {
    return _companion_thread;
  }


  virtual Name kind() const override {
    return CollectedHeap::ThirdPartyHeap;
  }
  virtual const char* name() const override {
    return "MMTk";
  }
  static const char* version();

  virtual size_t capacity() const override;
  virtual size_t used() const override;

  virtual bool is_maximal_no_gc() const override;

  virtual size_t max_capacity() const override;
  virtual bool is_in(const void* p) const override;

  // The amount of space available for thread-local allocation buffers.
  virtual size_t tlab_capacity(Thread *thr) const override;

  // The amount of used space for thread-local allocation buffers for the given thread.
  virtual size_t tlab_used(Thread *thr) const override;

  void new_collector_thread() {
    _n_workers += 1;
  }

  Monitor* gc_lock() {
    return _gc_lock;
  }

  bool can_elide_tlab_store_barriers() const;


  bool can_elide_initializing_store_barrier(oop new_obj);

  // mark to be thus strictly sequenced after the stores.
  bool card_mark_must_follow_store() const;

  virtual void collect(GCCause::Cause cause) override;

  // Perform a full collection
  virtual void do_full_collection(bool clear_all_soft_refs) override;

  virtual void collect_as_vm_thread(GCCause::Cause cause) override;


  virtual SoftRefPolicy* soft_ref_policy() override;

  virtual GrowableArray<GCMemoryManager*> memory_managers() override;
  virtual GrowableArray<MemoryPool*> memory_pools() override;

  // Iterate over all objects, calling "cl.do_object" on each.
  virtual void object_iterate(ObjectClosure* cl) override;

  void pin_object(JavaThread* thread, oop obj);
  void unpin_object(JavaThread* thread, oop obj);

  virtual void prepare_for_verify() override;

  virtual void register_new_weak_handle(oop* handle) /*override*/;

private:

  virtual void initialize_serviceability() override;

  void set_mmtk_options(bool set_defaults);

public:

  // Print heap information on the given outputStream.
  virtual void print_on(outputStream* st) const override;

  // Iterator for all GC threads (other than VM thread)
  virtual void gc_threads_do(ThreadClosure* tc) const override;

  // Print any relevant tracing info that flags imply.
  // Default implementation does nothing.
  virtual void print_tracing_info() const override;

  bool print_location(outputStream* st, void* addr) const;

  bool requires_barriers(stackChunkOop obj) const;

  virtual void register_nmethod(nmethod* nm) override;
  virtual void unregister_nmethod(nmethod* nm) override;

  virtual void verify_nmethod(nmethod* nm) override;

  // An object is scavengable if its location may move during a scavenge.
  // (A scavenge is a GC which is not a full GC.)
  inline bool is_scavengable(oop obj) { return true; }

  // Heap verification
  virtual void verify(VerifyOption option) override;

  virtual void post_initialize() override;

  void scan_roots(OopClosure& cl);

  void scan_roots_in_all_mutator_threads(OopClosure& cl);

  void scan_code_cache_roots(OopClosure& cl);
  void scan_class_loader_data_graph_roots(OopClosure& cl);
  void scan_oop_storage_set_roots(OopClosure& cl);
  void scan_weak_processor_roots(OopClosure& cl);
  void scan_vm_thread_roots(OopClosure& cl);
};


#endif // MMTK_OPENJDK_MMTK_HEAP_HPP
