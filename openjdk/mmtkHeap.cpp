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
 * Please contactSUn 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "aot/aotLoader.hpp"
#include "classfile/stringTable.hpp"
#include "code/codeCache.hpp"
#include "gc/shared/gcHeapSummary.hpp"
#include "gc/shared/gcLocker.inline.hpp"
#include "gc/shared/gcWhen.hpp"
#include "gc/shared/strongRootsScope.hpp"
#include "gc/shared/weakProcessor.hpp"
#include "logging/log.hpp"
#include "mmtk.h"
#include "mmtkHeap.hpp"
#include "mmtkMutator.hpp"
#include "mmtkUpcalls.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/atomic.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/thread.hpp"
#include "runtime/vmThread.hpp"
#include "services/management.hpp"
#include "services/memoryManager.hpp"
#include "services/memTracker.hpp"
#include "utilities/vmError.hpp"
/*
needed support from rust
heap capacity
used bytes
starting heap address
ending heap address
last gc time
object iterator??!!
*/

//mmtkGCTaskManager* MMTkHeap::_mmtk_gc_task_manager = NULL;


MMTkHeap* MMTkHeap::_heap = NULL;

MMTkHeap::MMTkHeap(MMTkCollectorPolicy* policy) : CollectedHeap(), _last_gc_time(0), _collector_policy(policy),  _num_root_scan_tasks(0), _n_workers(0), _gc_lock(new Monitor(Mutex::safepoint, "MMTkHeap::_gc_lock", true, Monitor::_safepoint_check_sometimes))
// , _par_state_string(StringTable::weak_storage())
{
  _heap = this;
}

jint MMTkHeap::initialize() {
  assert(!UseTLAB , "should disable UseTLAB");
  assert(!UseCompressedOops , "should disable CompressedOops");
  assert(!UseCompressedClassPointers , "should disable UseCompressedClassPointers");
  const size_t heap_size = collector_policy()->max_heap_byte_size();
  //  printf("policy max heap size %zu, min heap size %zu\n", heap_size, collector_policy()->min_heap_byte_size());
  size_t mmtk_heap_size = heap_size;
  /*forcefully*/ //mmtk_heap_size = (1<<31) -1;

  openjdk_gc_init(&mmtk_upcalls, mmtk_heap_size);
  // Cache the value here. It is a constant depending on the selected plan. The plan won't change from now, so value won't change.
  MMTkMutatorContext::max_non_los_default_alloc_bytes = get_max_non_los_default_alloc_bytes();

  //ReservedSpace heap_rs = Universe::reserve_heap(mmtk_heap_size, _collector_policy->heap_alignment());

  //printf("inside mmtkHeap.cpp.. reserved base %x size %u \n", heap_rs.base(), heap_rs.size());

  //os::trace_page_sizes("Heap",
  //                     _collector_policy->min_heap_byte_size(),
  //                     mmtk_heap_size,
  //                     collector_policy()->space_alignment(),
  //                     heap_rs.base(),
  //                     heap_rs.size());

  //_start = (HeapWord*)heap_rs.base();
  //_end = (HeapWord*)(heap_rs.base() + heap_rs.size());

  _start = (HeapWord*) starting_heap_address();
  _end = (HeapWord*) last_heap_address();
  //  printf("start: %p, end: %p\n", _start, _end);

  initialize_reserved_region(_start, _end);


  MMTkBarrierSet* const barrier_set = new MMTkBarrierSet(reserved_region());
  //barrier_set->initialize();
  BarrierSet::set_barrier_set(barrier_set);

  // Set up the GCTaskManager
  //  _mmtk_gc_task_manager = mmtkGCTaskManager::create(ParallelGCThreads);
  return JNI_OK;

}

void MMTkHeap::schedule_finalizer() {
  MMTkFinalizerThread::instance->schedule();
}

void MMTkHeap::post_initialize() {
  CollectedHeap::post_initialize();
}

void MMTkHeap::enable_collection() {
  // Initialize finalizer thread before enable_collection().
  // Otherwise it is possible that we schedule finalizer (during a GC) before the finalizer thread is ready.
  MMTkFinalizerThread::initialize();

  ::enable_collection(0);
}

////Previously pure abstract methods--

size_t MMTkHeap::capacity() const {
  return max_capacity();
}

size_t MMTkHeap::max_capacity() const {
  //used by jvm

  // Support for java.lang.Runtime.maxMemory():  return the maximum amount of
  // memory that the vm could make available for storing 'normal' java objects.
  // This is based on the reserved address space, but should not include space
  // that the vm uses internally for bookkeeping or temporary storage
  // (e.g., in the case of the young gen, one of the survivor spaces).

  return openjdk_max_capacity();
}

size_t MMTkHeap::used() const {
  //has to be implemented. used in universe.cpp
  //in ps : young_gen()->used_in_bytes() + old_gen()->used_in_bytes()
  //guarantee(false, "error not yet implemented");
  return used_bytes();
}

bool MMTkHeap::is_maximal_no_gc() const {
  //has to be implemented. used in collectorpolicy.cpp in shared

  // Return "true" if the part of the heap that allocates Java
  // objects has reached the maximal committed limit that it can
  // reach, without a garbage collection.

  //can be implemented like if(used()>= capacity()-X){}
  return false;
}

bool MMTkHeap::is_in(const void* p) const {
  //used in collected heap , jvmruntime and many more.........

  // Returns "TRUE" iff "p" points into the committed areas of the heap.
  //we need starting and endinf address of the heap

  // in ps : char* const cp = (char*)p;
  //return cp >= committed_low_addr() && cp < committed_high_addr();

  //guarantee(false, "is in not supported");
  return is_mapped_object(const_cast<void *>(p));
}

bool MMTkHeap::is_in_reserved(const void* p) const {
  //printf("calling MMTkHeap::is_in_reserved\n");
  return is_in(p);
}

bool MMTkHeap::supports_tlab_allocation() const {
  //returning false is good enough...used in universe.cpp
  return false;
}

// The amount of space available for thread-local allocation buffers.
size_t MMTkHeap::tlab_capacity(Thread *thr) const {
  //no need to further implement but we need UseTLAB=False
  guarantee(false, "tlab_capacity not supported");
  return 0;
}

// The amount of used space for thread-local allocation buffers for the given thread.
size_t MMTkHeap::tlab_used(Thread *thr) const {
  //no need to further implement but we need UseTLAB=False
  guarantee(false, "tlab_used not supported");
  return 0;
}


// Can a compiler initialize a new object without store barriers?
// This permission only extends from the creation of a new object
// via a TLAB up to the first subsequent safepoint. //However, we will not use tlab
bool MMTkHeap::can_elide_tlab_store_barriers() const {  //OK
  return true;
}


bool MMTkHeap::can_elide_initializing_store_barrier(oop new_obj) { //OK
  //guarantee(false, "can elide initializing store barrier not supported");
  return false;
}

// mark to be thus strictly sequenced after the stores.
bool MMTkHeap::card_mark_must_follow_store() const { //OK
  return false;
}

void MMTkHeap::collect(GCCause::Cause cause) {//later when gc is implemented in rust
  handle_user_collection_request((MMTk_Mutator) &Thread::current()->third_party_heap_mutator);
  // guarantee(false, "collect not supported");
}

// Perform a full collection
void MMTkHeap::do_full_collection(bool clear_all_soft_refs) {//later when gc is implemented in rust
  // guarantee(false, "do full collection not supported");

  // handle_user_collection_request((MMTk_Mutator) &Thread::current()->third_party_heap_mutator);
}


// Return the CollectorPolicy for the heap
CollectorPolicy* MMTkHeap::collector_policy() const {return _collector_policy;}//OK

SoftRefPolicy* MMTkHeap::soft_ref_policy() {return _soft_ref_policy;}//OK

GrowableArray<GCMemoryManager*> MMTkHeap::memory_managers() {//may cause error

  GrowableArray<GCMemoryManager*> memory_managers(1);
  memory_managers.append(_mmtk_manager);
  return memory_managers;
}
GrowableArray<MemoryPool*> MMTkHeap::memory_pools() {//may cause error

  GrowableArray<MemoryPool*> memory_pools(1);
  memory_pools.append(_mmtk_pool);
  return memory_pools;
}

// Iterate over all objects, calling "cl.do_object" on each.
void MMTkHeap::object_iterate(ObjectClosure* cl) { //No need to implement.Traced whole path.Only other heaps call it.
  guarantee(false, "object iterate not supported");
}

// Similar to object_iterate() except iterates only
// over live objects.
void MMTkHeap::safe_object_iterate(ObjectClosure* cl) { //not sure..many dependencies from vm
  guarantee(false, "safe object iterate not supported");
}

HeapWord* MMTkHeap::block_start(const void* addr) const {//OK
  guarantee(false, "block start not supported");
  return NULL;
}

size_t MMTkHeap::block_size(const HeapWord* addr) const { //OK
  guarantee(false, "block size not supported");
  return 0;
}

bool MMTkHeap::block_is_obj(const HeapWord* addr) const { //OK
  guarantee(false, "block is obj not supported");
  return false;
}

jlong MMTkHeap::millis_since_last_gc() {//later when gc is implemented in rust
  jlong ret_val = (os::javaTimeNanos() / NANOSECS_PER_MILLISEC) - _last_gc_time;
  if (ret_val < 0) {
    log_warning(gc)("millis_since_last_gc() would return : " JLONG_FORMAT
                    ". returning zero instead.", ret_val);
    return 0;
  }
  return ret_val;
}


void MMTkHeap::prepare_for_verify() {
  // guarantee(false, "prepare for verify not supported");
}


void MMTkHeap::initialize_serviceability() {//OK


  _mmtk_pool = new MMTkMemoryPool(_start, _end,  "MMTk pool",collector_policy()->min_heap_byte_size(), false);

  _mmtk_manager = new GCMemoryManager("MMTk GC", "end of GC");
  _mmtk_manager->add_pool(_mmtk_pool);
}

// Print heap information on the given outputStream.
void MMTkHeap::print_on(outputStream* st) const {guarantee(false, "print on not supported");}


// Print all GC threads (other than the VM thread)
// used by this heap.
void MMTkHeap::print_gc_threads_on(outputStream* st) const {guarantee(false, "print gc threads on not supported");}

// Iterator for all GC threads (other than VM thread)
void MMTkHeap::gc_threads_do(ThreadClosure* tc) const {
  // guarantee(false, "gc threads do not supported");
}

// Print any relevant tracing info that flags imply.
// Default implementation does nothing.
void MMTkHeap::print_tracing_info() const {
  //guarantee(false, "print tracing info not supported");
}


// An object is scavengable if its location may move during a scavenge.
// (A scavenge is a GC which is not a full GC.)
bool MMTkHeap::is_scavengable(oop obj) {return false;}
// Registering and unregistering an nmethod (compiled code) with the heap.
// Override with specific mechanism for each specialized heap type.

// Heap verification
void MMTkHeap::verify(VerifyOption option) {}

template<int MAX_TASKS = 12>
struct MMTkRootScanWorkScope {
  int* _num_root_scan_tasks;
  int _current_task_ordinal;
  MMTkRootScanWorkScope(int* num_root_scan_tasks): _num_root_scan_tasks(num_root_scan_tasks), _current_task_ordinal(0) {
    _current_task_ordinal = Atomic::add(1, _num_root_scan_tasks);
    if (_current_task_ordinal == 1) {
      nmethod::oops_do_marking_prologue();
    }
  }
  ~MMTkRootScanWorkScope() {
    if (_current_task_ordinal == MAX_TASKS) {
      _current_task_ordinal = 0;
      nmethod::oops_do_marking_epilogue();
    }
  }
};

void MMTkHeap::scan_static_roots(OopClosure& cl) {
}


void MMTkHeap::scan_universe_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  Universe::oops_do(&cl);
}
void MMTkHeap::scan_jni_handle_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  JNIHandles::oops_do(&cl);
}
void MMTkHeap::scan_object_synchronizer_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  ObjectSynchronizer::oops_do(&cl);
}
void MMTkHeap::scan_management_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  Management::oops_do(&cl);
}
void MMTkHeap::scan_jvmti_export_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  JvmtiExport::oops_do(&cl);
}
void MMTkHeap::scan_aot_loader_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  AOTLoader::oops_do(&cl);
}
void MMTkHeap::scan_system_dictionary_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  SystemDictionary::oops_do(&cl);
}
void MMTkHeap::scan_code_cache_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  CodeBlobToOopClosure cb_cl(&cl, true);
  {
    MutexLockerEx lock(CodeCache_lock, Mutex::_no_safepoint_check_flag);
    CodeCache::scavenge_root_nmethods_do(&cb_cl);
    CodeCache::blobs_do(&cb_cl);
  }
}
void MMTkHeap::scan_string_table_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  StringTable::oops_do(&cl);
}
void MMTkHeap::scan_class_loader_data_graph_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  CLDToOopClosure cld_cl(&cl, false);
  ClassLoaderDataGraph::cld_do(&cld_cl);
}
void MMTkHeap::scan_weak_processor_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  WeakProcessor::oops_do(&cl); // (really needed???)
}
void MMTkHeap::scan_vm_thread_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  VMThread::vm_thread()->oops_do(&cl, NULL);
}

void MMTkHeap::scan_global_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);

  CodeBlobToOopClosure cb_cl(&cl, true);
  CLDToOopClosure cld_cl(&cl, false);

  Universe::oops_do(&cl);
  JNIHandles::oops_do(&cl);
  ObjectSynchronizer::oops_do(&cl);
  Management::oops_do(&cl);
  JvmtiExport::oops_do(&cl);
  AOTLoader::oops_do(&cl);
  SystemDictionary::oops_do(&cl);
  {
    MutexLockerEx lock(CodeCache_lock, Mutex::_no_safepoint_check_flag);
    CodeCache::blobs_do(&cb_cl);
  }

  OopStorage::ParState<false, false> _par_state_string(StringTable::weak_storage());
  StringTable::possibly_parallel_oops_do(&_par_state_string, &cl);

  // if (!_root_tasks->is_task_claimed(MMTk_ClassLoaderDataGraph_oops_do)) ClassLoaderDataGraph::roots_cld_do(&cld_cl, &cld_cl);
  ClassLoaderDataGraph::cld_do(&cld_cl);

  WeakProcessor::oops_do(&cl); // (really needed???)
}

void MMTkHeap::scan_thread_roots(OopClosure& cl) {
  ResourceMark rm;
  MMTkRootScanWorkScope<> root_scan_work(&_num_root_scan_tasks);
  Threads::possibly_parallel_oops_do(false, &cl, NULL);
}

void MMTkHeap::scan_roots(OopClosure& cl) {
  // Need to tell runtime we are about to walk the roots with 1 thread
  StrongRootsScope scope(1);
  CLDToOopClosure cld_cl(&cl, false);
  CodeBlobToOopClosure cb_cl(&cl, true);

  // Static Roots
  ClassLoaderDataGraph::cld_do(&cld_cl);

  // Thread Roots
  bool is_parallel = false;
  Threads::possibly_parallel_oops_do(is_parallel, &cl, &cb_cl);

  // Global Roots
  Universe::oops_do(&cl);
  JNIHandles::oops_do(&cl);
  ObjectSynchronizer::oops_do(&cl);
  Management::oops_do(&cl);
  JvmtiExport::oops_do(&cl);
  if (UseAOT) AOTLoader::oops_do(&cl);
  SystemDictionary::oops_do(&cl);
  {
    MutexLockerEx lock(CodeCache_lock, Mutex::_no_safepoint_check_flag);
    CodeCache::blobs_do(&cb_cl);
  }
  if (is_parallel) {
    StringTable::possibly_parallel_oops_do(NULL, &cl);
  } else {
    StringTable::oops_do(&cl);
  }

  // Weak refs (really needed???)
  WeakProcessor::oops_do(&cl);
}

HeapWord* MMTkHeap::mem_allocate(size_t size, bool* gc_overhead_limit_was_exceeded) {
  HeapWord* obj = Thread::current()->third_party_heap_mutator.alloc(size << LogHeapWordSize);
  // post_alloc(Thread::current()->mmtk_mutator(), obj_ptr, NULL, size << LogHeapWordSize, 0);
  // printf("offset: %ld\n", ((size_t) &Thread::current()->third_party_heap_mutator) - ((size_t) Thread::current()));
  guarantee(obj, "MMTk gave us null!");
  return obj;
}

HeapWord* MMTkHeap::mem_allocate_nonmove(size_t size, bool* gc_overhead_limit_was_exceeded) {
  return Thread::current()->third_party_heap_mutator.alloc(size << LogHeapWordSize, AllocatorLos);
}

void (*MMTkHeap::_create_stack_scan_work)(void*) = NULL;

void MMTkHeap::report_java_thread_yield(JavaThread* thread) {
  if (_create_stack_scan_work != NULL) _create_stack_scan_work((void*) &thread->third_party_heap_mutator);
}

/*
 * files with prints currently:
 * collectedHeap.inline.hpp, mmtkHeap.cpp,
 */
