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
#include "memory/resourceArea.hpp"
#include "mmtk.h"
#include "mmtkHeap.hpp"
#include "mmtkMutator.hpp"
#include "mmtkUpcalls.hpp"
#include "mmtkVMCompanionThread.hpp"
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

MMTkHeap::MMTkHeap(MMTkCollectorPolicy* policy) :
  CollectedHeap(),
  _last_gc_time(0),
  _collector_policy(policy),
  _num_root_scan_tasks(0),
  _n_workers(0),
  _gc_lock(new Monitor(Mutex::safepoint, "MMTkHeap::_gc_lock", true, Monitor::_safepoint_check_sometimes)),
  _soft_ref_policy()
{
  _heap = this;
}

jint MMTkHeap::initialize() {
  assert(!UseTLAB , "should disable UseTLAB");
  const size_t min_heap_size = collector_policy()->min_heap_byte_size();
  const size_t max_heap_size = collector_policy()->max_heap_byte_size();
  //  printf("policy max heap size %zu, min heap size %zu\n", heap_size, collector_policy()->min_heap_byte_size());

  if (UseCompressedOops) mmtk_enable_compressed_oops();

  // Note that MMTk options may be set from several different sources, with increasing priorities:
  // 1. Default values defined in mmtk::util::options::Options
  // 2. Default values defined in ThirdPartyHeapArguments::initialize
  // 3. Environment variables starting with `MMTK_`
  // 4. Command line arguments
  // We need to be careful about the order in which we set the options in the MMTKBuilder so that
  // the values from the highest priority source will take effect.

  // Priority 2: Set options in MMTKBuilder to OpenJDK's default options.
  set_mmtk_options(true);

  // Priority 3: Read MMTk options from environment variables (such as `MMTK_THREADS`).
  mmtk_builder_read_env_var_settings();

  // Priority 4: Pass non-default OpenJDK options (may be set from command line) to MMTk options.
  set_mmtk_options(false);

  if (ThirdPartyHeapOptions != NULL) {
    bool set_options = process_bulk(strdup(ThirdPartyHeapOptions));
    guarantee(set_options, "Failed to set MMTk options. Please check if the options are valid: %s\n", ThirdPartyHeapOptions);
  }

  // Set heap size
  bool set_heap_size = mmtk_set_heap_size(min_heap_size, max_heap_size);
  guarantee(set_heap_size, "Failed to set MMTk heap size. Please check if the heap size is valid: min = %ld, max = %ld\n", min_heap_size, max_heap_size);

  openjdk_gc_init(&mmtk_upcalls);
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
  if (UseCompressedOops) {
    Universe::set_narrow_oop_base((address) mmtk_narrow_oop_base());
    Universe::set_narrow_oop_shift(mmtk_narrow_oop_shift());
  }

  initialize_reserved_region(_start, _end);


  MMTkBarrierSet* const barrier_set = new MMTkBarrierSet(reserved_region());
  //barrier_set->initialize();
  BarrierSet::set_barrier_set(barrier_set);

  _companion_thread = new MMTkVMCompanionThread();
  if (!os::create_thread(_companion_thread, os::pgc_thread)) {
    fprintf(stderr, "Failed to create thread");
    guarantee(false, "panic");
  }
  os::start_thread(_companion_thread);
  // Set up the GCTaskManager
  //  _mmtk_gc_task_manager = mmtkGCTaskManager::create(ParallelGCThreads);
  return JNI_OK;

}

void MMTkHeap::set_mmtk_options(bool set_defaults) {
  // If set_defaults is true, we only set default options here;
  // if it is false, we only set options that has been overridden by command line.
  if (FLAG_IS_DEFAULT(ParallelGCThreads) == set_defaults) {
    mmtk_builder_set_threads(ParallelGCThreads);
  }

  if (FLAG_IS_DEFAULT(UseTransparentHugePages) == set_defaults) {
    mmtk_builder_set_transparent_hugepages(UseTransparentHugePages);
  }
}


const char* MMTkHeap::version() {
  return get_mmtk_version();
}

void MMTkHeap::schedule_finalizer() {
  MMTkFinalizerThread::instance->schedule();
}

void MMTkHeap::post_initialize() {
  CollectedHeap::post_initialize();
  if (UseCompressedOops) {
    mmtk_set_compressed_klass_base_and_shift((void*) Universe::narrow_klass_base(), (size_t) Universe::narrow_klass_shift());
  }
}

void MMTkHeap::enable_collection() {
  // Initialize finalizer thread before enable_collection().
  // Otherwise it is possible that we schedule finalizer (during a GC) before the finalizer thread is ready.
  MMTkFinalizerThread::initialize();

  ::initialize_collection(0);
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
  return is_in_mmtk_spaces(const_cast<void *>(p));
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

SoftRefPolicy* MMTkHeap::soft_ref_policy() {return &_soft_ref_policy;}//OK

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


// Registering and unregistering an nmethod (compiled code) with the heap.
// Override with specific mechanism for each specialized heap type.
class MMTkRegisterNMethodOopClosure: public OopClosure {
  template <class T> void do_oop_work(T* p, bool narrow) {
    if (UseCompressedOops && !narrow) {
      guarantee((uintptr_t(p) & (1ull << 63)) == 0, "test");
      auto tagged_p = (T*) (uintptr_t(p) | (1ull << 63));
      mmtk_add_nmethod_oop((void*) tagged_p);
    } else {
      mmtk_add_nmethod_oop((void*) p);
    }
  }
public:
  void do_oop(oop* p)       { do_oop_work(p, false); }
  void do_oop(narrowOop* p) { do_oop_work(p, true); }
};


void MMTkHeap::register_nmethod(nmethod* nm) {
  // Scan and report pointers in this nmethod
  MMTkRegisterNMethodOopClosure reg_cl;
  nm->oops_do(&reg_cl);
  // Register the nmethod
  mmtk_register_nmethod((void*) nm);
}

void MMTkHeap::unregister_nmethod(nmethod* nm) {
  mmtk_unregister_nmethod((void*) nm);
}

// Heap verification
void MMTkHeap::verify(VerifyOption option) {}

void MMTkHeap::scan_universe_roots(OopClosure& cl) {
  Universe::oops_do(&cl);
}
void MMTkHeap::scan_jni_handle_roots(OopClosure& cl) {
  JNIHandles::oops_do(&cl);
}
void MMTkHeap::scan_object_synchronizer_roots(OopClosure& cl) {
  ObjectSynchronizer::oops_do(&cl);
}
void MMTkHeap::scan_management_roots(OopClosure& cl) {
  Management::oops_do(&cl);
}
void MMTkHeap::scan_jvmti_export_roots(OopClosure& cl) {
  JvmtiExport::oops_do(&cl);
}
void MMTkHeap::scan_aot_loader_roots(OopClosure& cl) {
  AOTLoader::oops_do(&cl);
}
void MMTkHeap::scan_system_dictionary_roots(OopClosure& cl) {
  SystemDictionary::oops_do(&cl);
}
void MMTkHeap::scan_code_cache_roots(OopClosure& cl) {
  MarkingCodeBlobClosure cb_cl(&cl, false);
  CodeCache::blobs_do(&cb_cl);
}
void MMTkHeap::scan_string_table_roots(OopClosure& cl) {
  StringTable::oops_do(&cl);
}
void MMTkHeap::scan_class_loader_data_graph_roots(OopClosure& cl) {
  CLDToOopClosure cld_cl(&cl, false);
  ClassLoaderDataGraph::cld_do(&cld_cl);
}
void MMTkHeap::scan_weak_processor_roots(OopClosure& cl) {
  ResourceMark rm;
  WeakProcessor::oops_do(&cl); // (really needed???)
}
void MMTkHeap::scan_vm_thread_roots(OopClosure& cl) {
  ResourceMark rm;
  VMThread::vm_thread()->oops_do(&cl, NULL);
}

void MMTkHeap::scan_roots_in_all_mutator_threads(OopClosure& cl) {
  ResourceMark rm;
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
  return obj;
}

HeapWord* MMTkHeap::mem_allocate_nonmove(size_t size, bool* gc_overhead_limit_was_exceeded) {
  return Thread::current()->third_party_heap_mutator.alloc(size << LogHeapWordSize, AllocatorLos);
}

/*
 * files with prints currently:
 * collectedHeap.inline.hpp, mmtkHeap.cpp,
 */
