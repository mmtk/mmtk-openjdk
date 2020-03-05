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

#ifndef SHARE_VM_GC_MMTK_MMTKHEAP_HPP
#define SHARE_VM_GC_MMTK_MMTKHEAP_HPP

#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/collectorPolicy.hpp"
#include "gc/shared/gcPolicyCounters.hpp"
#include "gc/shared/gcWhen.hpp"
#include "gc/shared/strongRootsScope.hpp"
#include "memory/metaspace.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/ostream.hpp"
#include "mmtkMemoryPool.hpp"
#include "memory/iterator.hpp"
#include "gc/shared/workgroup.hpp"
#include "mmtkCollectorPolicy.hpp"


class GCMemoryManager;
class MemoryPool;
//class mmtkGCTaskManager;

class MMTkHeap : public CollectedHeap {
    MMTkCollectorPolicy* _collector_policy;
    SoftRefPolicy* _soft_ref_policy;
    MMTkMemoryPool* _mmtk_pool;
    GCMemoryManager* _mmtk_manager;
    HeapWord* _start;
    HeapWord* _end;
    static MMTkHeap* _heap;
    SubTasksDone* _root_tasks;
    size_t _n_workers;
    Monitor* _gc_lock;

  enum mmtk_strong_roots_tasks {
    MMTk_Universe_oops_do,
    MMTk_JNIHandles_oops_do,
    MMTk_ObjectSynchronizer_oops_do,
    MMTk_Management_oops_do,
    MMTk_SystemDictionary_oops_do,
    MMTk_ClassLoaderDataGraph_oops_do,
    MMTk_jvmti_oops_do,
    MMTk_CodeCache_oops_do,
    MMTk_aot_oops_do,
    MMTk_WeakProcessor_oops_do,
    // Leave this one last.
    MMTk_NumElements
  };
    
private:
   // static mmtkGCTaskManager* _mmtk_gc_task_manager;
    

public:
     
  MMTkHeap(MMTkCollectorPolicy* policy) : CollectedHeap(), _collector_policy(policy), _root_tasks(new SubTasksDone(MMTk_NumElements)), _n_workers(0), _gc_lock(new Monitor(Mutex::safepoint, "MMTkHeap::_gc_lock", true, Monitor::_safepoint_check_sometimes)) {
    _heap = this;
  }

  inline static MMTkHeap* heap() {
    return _heap;
  }
     
  static HeapWord* allocate_from_tlab(Klass* klass, Thread* thread, size_t size);
 
  jint initialize();
  
  HeapWord* mem_allocate(size_t size, bool* gc_overhead_limit_was_exceeded);
  
  
  
  Name kind() const {
    return CollectedHeap::MMTkHeap;
  }
  const char* name() const {
    return "MMTk";
  }
  
  size_t capacity() const;
  size_t used() const;
  
  bool is_maximal_no_gc() const;

  size_t max_capacity() const;
  bool is_in(const void* p) const;
  bool is_in_reserved(const void* p) const;
  bool supports_tlab_allocation() const;

  // The amount of space available for thread-local allocation buffers.
  size_t tlab_capacity(Thread *thr) const;

  // The amount of used space for thread-local allocation buffers for the given thread.
  size_t tlab_used(Thread *thr) const;
  
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

  void collect(GCCause::Cause cause);

  // Perform a full collection
  void do_full_collection(bool clear_all_soft_refs);


  // Return the CollectorPolicy for the heap
  CollectorPolicy* collector_policy() const ;

  SoftRefPolicy* soft_ref_policy();

  GrowableArray<GCMemoryManager*> memory_managers() ;
  GrowableArray<MemoryPool*> memory_pools();

  // Iterate over all objects, calling "cl.do_object" on each.
  void object_iterate(ObjectClosure* cl);

  // Similar to object_iterate() except iterates only
  // over live objects.
  void safe_object_iterate(ObjectClosure* cl) ;

  HeapWord* block_start(const void* addr) const ;

  size_t block_size(const HeapWord* addr) const ;

  bool block_is_obj(const HeapWord* addr) const;

  jlong millis_since_last_gc() ;

  void prepare_for_verify() ;


 private:

  void initialize_serviceability() ;

 public:
  
  // Print heap information on the given outputStream.
  void print_on(outputStream* st) const ;


  // Print all GC threads (other than the VM thread)
  // used by this heap.
  void print_gc_threads_on(outputStream* st) const;

  // Iterator for all GC threads (other than VM thread)
  void gc_threads_do(ThreadClosure* tc) const;

  // Print any relevant tracing info that flags imply.
  // Default implementation does nothing.
  void print_tracing_info() const ;


  // An object is scavengable if its location may move during a scavenge.
  // (A scavenge is a GC which is not a full GC.)
  bool is_scavengable(oop obj);
  // Registering and unregistering an nmethod (compiled code) with the heap.
  // Override with specific mechanism for each specialized heap type.

  // Heap verification
  void verify(VerifyOption option);

  void post_initialize();

  void scan_roots(OopClosure& cl);

  void scan_static_roots(OopClosure& cl);
  void scan_global_roots(OopClosure& cl);
  void scan_thread_roots(OopClosure& cl);
};


#endif // SHARE_VM_GC_MMTK_MMTKHEAP_HPP
