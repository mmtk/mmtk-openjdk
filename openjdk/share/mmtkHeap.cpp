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
#include "classfile/stringTable.hpp"
#include "classfile/classLoaderDataGraph.hpp"
#include "code/codeCache.hpp"
#include "gc/shared/gcArguments.hpp"
#include "gc/shared/gcHeapSummary.hpp"
#include "gc/shared/gcLocker.inline.hpp"
#include "gc/shared/gcWhen.hpp"
#include "gc/shared/oopStorageSet.inline.hpp"
#include "gc/shared/scavengableNMethods.hpp"
#include "gc/shared/strongRootsScope.hpp"
#include "gc/shared/weakProcessor.hpp"
#include "gc/shared/gcLocker.inline.hpp"
#include "logging/log.hpp"
#include "memory/resourceArea.hpp"
#include "mmtk.h"
#include "mmtkHeap.hpp"
#include "mmtkMutator.hpp"
#include "mmtkUpcalls.hpp"
#include "mmtkVMCompanionThread.hpp"
#include "oops/oop.inline.hpp"
#ifdef COMPILER2
#include "opto/runtime.hpp"
#endif
#include "prims/jvmtiExport.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/atomic.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/thread.hpp"
#include "runtime/threads.hpp"
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

// ReservedHeapSpace will actually do the mmap when constructed.
// We reimplement it without mmaping, and fill in the fields manually using data from MMTk core.
class MMTkReservedHeapSpace : public ReservedHeapSpace {
public:
  MMTkReservedHeapSpace(bool use_compressed_oops)
    : ReservedHeapSpace(0, 0, 0) // When `size == 0`, the constructor of ReservedHeapSpace will return immediately.
  {
    uintptr_t start = (uintptr_t)starting_heap_address();
    uintptr_t end = (uintptr_t)last_heap_address();
    uintptr_t size = end - start;

    _base = (char*)start;
    _size = size;
    _noaccess_prefix = 0;
    _alignment = HeapAlignment;
    _page_size = 4096; // MMTk has been assuming 4096-byte pages, which is not always true.
    _special = false; // from jfrVirtualMemory.cpp: "ReservedSpaces marked as special will have the entire memory pre-committed."
    _fd_for_heap = -1; // The MMTk heap is not backed by a file descriptor.
  }
};

MMTkHeap* MMTkHeap::_heap = NULL;

MMTkHeap::MMTkHeap() :
  CollectedHeap(),
  _mmtk_pool(nullptr),
  _mmtk_manager(nullptr),
  _n_workers(0),
  _gc_lock(new Monitor(Mutex::nosafepoint, "MMTkHeap::_gc_lock", true)),
  _num_root_scan_tasks(0),
  _companion_thread(nullptr),
  _soft_ref_policy(),
  _last_gc_time(0)
{
  _heap = this;
}

static void set_bool_option_from_env_var(const char *name, bool *var) {
  const char *env_var = getenv(name);
  if (env_var != NULL) {
    if (strcmp(env_var, "true") == 0 || strcmp(env_var, "yes") == 0 || strcmp(env_var, "on") == 0 || strcmp(env_var, "1") == 0) {
      *var = true;
    } else if (strcmp(env_var, "false") == 0 || strcmp(env_var, "no") == 0 || strcmp(env_var, "off") == 0 || strcmp(env_var, "0") == 0) {
      *var = false;
    } else {
      fprintf(stderr, "Unexpected value for env var %s: %s\n", name, env_var);
      abort();
    }
  }
}

jint MMTkHeap::initialize() {
  assert(!UseTLAB , "should disable UseTLAB");
  assert(AllocateHeapAt == nullptr, "MMTk does not support file-backed heap.");

  set_bool_option_from_env_var("MMTK_ENABLE_ALLOCATION_FASTPATH", &mmtk_enable_allocation_fastpath);
  set_bool_option_from_env_var("MMTK_ENABLE_BARRIER_FASTPATH", &mmtk_enable_barrier_fastpath);

  const size_t min_heap_size = MinHeapSize;
  const size_t max_heap_size = MaxHeapSize;

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
    bool set_options = process_bulk(os::strdup(ThirdPartyHeapOptions));
    guarantee(set_options, "Failed to set MMTk options. Please check if the options are valid: %s\n", ThirdPartyHeapOptions);
  }

  // Set heap size
  bool set_heap_size = mmtk_set_heap_size(min_heap_size, max_heap_size);
  guarantee(set_heap_size, "Failed to set MMTk heap size. Please check if the heap size is valid: min = %ld, max = %ld\n", min_heap_size, max_heap_size);

  openjdk_gc_init(&mmtk_upcalls);

  // Cache the value here. It is a constant depending on the selected plan. The plan won't change from now, so value won't change.
  MMTkMutatorContext::max_non_los_default_alloc_bytes = get_max_non_los_default_alloc_bytes();

  // Compute the memory range.
  // Other GC in OpenJDK will do mmap when constructing ReservedHeapSpace, but MMTk does mmap internally.
  // So we construct our special MMTkReservedHeapSpace which doesn't actually do mmap.
  MMTkReservedHeapSpace heap_rs(UseCompressedOops);
  initialize_reserved_region(heap_rs); // initializes this->_reserved

  if (UseCompressedOops) {
    CompressedOops::initialize(heap_rs);

    // Assert the base and the shift computed by MMTk and OpenJDK match.
    address mmtk_base = (address)mmtk_narrow_oop_base();
    int mmtk_shift = mmtk_narrow_oop_shift();
    guarantee(mmtk_base == CompressedOops::base(), "MMTk and OpenJDK disagree with narrow oop base.  MMTk: %p, OpenJDK: %p", mmtk_base, CompressedOops::base());
    guarantee(mmtk_shift == CompressedOops::shift(), "MMTk and OpenJDK disagree with narrow oop shift.  MMTk: %d, OpenJDK: %d", mmtk_shift, CompressedOops::shift());
  }

  MMTkBarrierSet* const barrier_set = new MMTkBarrierSet(_reserved);
  BarrierSet::set_barrier_set(barrier_set);

  _companion_thread = new MMTkVMCompanionThread();
  if (!os::create_thread(_companion_thread, os::gc_thread)) {
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

class MMTkIsScavengable : public BoolObjectClosure {
  bool do_object_b(oop obj) {
    return true;
  }
};

static MMTkIsScavengable _is_scavengable;

void MMTkHeap::post_initialize() {
  CollectedHeap::post_initialize();

  ScavengableNMethods::initialize(&_is_scavengable);

  if (UseCompressedOops) {
    mmtk_set_compressed_klass_base_and_shift(
      (void*)CompressedKlassPointers::base(),
      (size_t)CompressedKlassPointers::shift());
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
  // Not supported yet.
}

// Similar to object_iterate() except iterates only
// over live objects.
void MMTkHeap::safe_object_iterate(ObjectClosure* cl) { //not sure..many dependencies from vm
  // Not supported yet.
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
  _mmtk_pool = new MMTkMemoryPool(_reserved, "MMTk pool", MinHeapSize, false);

  _mmtk_manager = new GCMemoryManager("MMTk GC");
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

// Used to print information about locations in the hs_err file.
bool MMTkHeap::print_location(outputStream* st, void* addr) const {
  guarantee(false, "print location not supported");
  return false;
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
// Callback for when nmethod is about to be deleted.
void MMTkHeap::flush_nmethod(nmethod* nm) {
}
void MMTkHeap::verify_nmethod(nmethod* nm) {
}

void MMTkHeap::unregister_nmethod(nmethod* nm) {
  mmtk_unregister_nmethod((void*) nm);
}

// Heap verification
void MMTkHeap::verify(VerifyOption option) {}

void MMTkHeap::scan_code_cache_roots(OopClosure& cl) {
  MarkingCodeBlobClosure cb_cl(&cl, false, true);
  CodeCache::blobs_do(&cb_cl);
}
void MMTkHeap::scan_class_loader_data_graph_roots(OopClosure& cl) {
  CLDToOopClosure cld_cl(&cl, false);
  ClassLoaderDataGraph::cld_do(&cld_cl);
}
void MMTkHeap::scan_oop_storage_set_roots(OopClosure& cl) {
  OopStorageSet::strong_oops_do(&cl);
}
void MMTkHeap::scan_weak_processor_roots(OopClosure& cl) {
  // XXX zixianc: I don't understand why this is removed in
  // 24b90dd889da0ea58aaa2b2311ded6f262573830
  // ResourceMark rm;
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
  {
    MutexLocker lock(CodeCache_lock, Mutex::_no_safepoint_check_flag);
    CodeCache::blobs_do(&cb_cl);
  }

  OopStorageSet::strong_oops_do(&cl);

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

bool MMTkHeap::requires_barriers(stackChunkOop obj) const {
  ShouldNotReachHere();
  return false;
}

void MMTkHeap::pin_object(JavaThread* thread, oop obj) {
  // TODO use mmtk-core pin_object
  GCLocker::lock_critical(thread);
}

void MMTkHeap::unpin_object(JavaThread* thread, oop obj) {
  // TODO use mmtk-core unpin_object
  GCLocker::unlock_critical(thread);
}


/*
 * files with prints currently:
 * collectedHeap.inline.hpp, mmtkHeap.cpp,
 */
