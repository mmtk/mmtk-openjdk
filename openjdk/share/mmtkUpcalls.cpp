/*
 * Copyright (c) 2017, Red Hat, Inc. and/or its affiliates.
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
 * Please contact Sun 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "mmtkBarrierSet.hpp"
#include "precompiled.hpp"
#include "classfile/classLoaderDataGraph.hpp"
#include "classfile/stringTable.hpp"
#include "code/nmethod.hpp"
#include "memory/iterator.inline.hpp"
#include "memory/resourceArea.hpp"
#include "mmtkCollectorThread.hpp"
#include "mmtkHeap.hpp"
#include "mmtkRootsClosure.hpp"
#include "mmtkUpcalls.hpp"
#include "mmtkVMCompanionThread.hpp"
#include "oops/access.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/atomic.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/thread.hpp"
#include "runtime/threads.hpp"
#include "runtime/thread.inline.hpp"
#include "runtime/threadSMR.hpp"
#include "runtime/vmThread.hpp"
#include "gc/shared/weakProcessor.hpp"
#include "prims/resolvedMethodTable.hpp"
#include "jfr/jfr.hpp"
#include "gc/shared/oopStorage.inline.hpp"
#include "runtime/osThread.hpp"
#include "utilities/debug.hpp"
#include "classfile/systemDictionary.hpp"
#include "prims/jvmtiExport.hpp"
#include "runtime/jniHandles.hpp"
#include "utilities/macros.hpp"
#include "gc/shared/classUnloadingContext.hpp"
#include "code/codeCache.hpp"
#if INCLUDE_JFR
#include "jfr/jfr.hpp"
#endif

using namespace JavaClassFile;

// Note: This counter must be accessed using the Atomic class.
static volatile size_t mmtk_start_the_world_count = 0;

class MMTkIsAliveClosure : public BoolObjectClosure {
public:
  inline virtual bool do_object_b(oop p) {
    if (p == NULL) return false;
    return mmtk_is_live((void*) p) != 0;
  }
};

class MMTkForwardClosure : public OopClosure {
 public:
  inline static size_t read_forwarding_word(oop o) {
    return *((size_t*) (void*) o);
  }
  inline static oop extract_forwarding_pointer(size_t status) {
    return (oop) (void*) (status << 8 >> 8);
  }
  inline static bool is_forwarded(size_t status) {
    return (status >> 56) != 0;
  }
  inline virtual void do_oop(oop* slot) {
    const auto o = *slot;
    if (o == NULL) return;
    const auto status = read_forwarding_word(o);
    if (is_forwarded(status)) {
      *slot = extract_forwarding_pointer(status);
    }
  }
  inline virtual void do_oop(narrowOop* slot) {
    narrowOop heap_oop = RawAccess<>::oop_load(slot);
    if (CompressedOops::is_null(heap_oop)) return;
    oop o = CompressedOops::decode_not_null(heap_oop);
    const auto status = read_forwarding_word(o);
    if (is_forwarded(status)) {
      RawAccess<>::oop_store(slot, CompressedOops::encode_not_null(extract_forwarding_pointer(status)));
    }
  }
};

class MMTkLXRFastIsAliveClosure : public BoolObjectClosure {
public:
  static inline bool rc_live(oop o) {
    return mmtk_get_rc((void*) o) != 0;
  }

  static inline bool is_forwarded(oop o) {
    return MMTkForwardClosure::is_forwarded(MMTkForwardClosure::read_forwarding_word(o));
  }

  inline virtual bool do_object_b(oop o) override {
    const uintptr_t v = uintptr_t((void*) o);
    // if (v >= 0x220000000000ULL || v < 0x20000000000ULL) return false;
    return o != NULL && (rc_live(o) || is_forwarded(o));
  }
};

class MMTkLXRFastUpdateClosure : public OopClosure {
 public:
  inline virtual void do_oop(oop* slot) override {
    const auto o = *slot;
    const uintptr_t v = uintptr_t((void*) o);
    if (o == NULL) {
      return;
    }
    const auto status = MMTkForwardClosure::read_forwarding_word(o);
    if (MMTkForwardClosure::is_forwarded(status)) {
      *slot = MMTkForwardClosure::extract_forwarding_pointer(status);
    } else if (!MMTkLXRFastIsAliveClosure::rc_live(o)) {
      *slot = NULL;
    }
  }
  inline virtual void do_oop(narrowOop* slot) override {
    narrowOop heap_oop = RawAccess<>::oop_load(slot);
    if (CompressedOops::is_null(heap_oop)) return;
    oop o = CompressedOops::decode_not_null(heap_oop);
    const uintptr_t v = uintptr_t((void*) o);
    const auto status = MMTkForwardClosure::read_forwarding_word(o);
    if (MMTkForwardClosure::is_forwarded(status)) {
      RawAccess<>::oop_store(slot, CompressedOops::encode_not_null(MMTkForwardClosure::extract_forwarding_pointer(status)));
    } else if (!MMTkLXRFastIsAliveClosure::rc_live(o)) {
      RawAccess<>::oop_store(slot, CompressedOops::encode(oop(NULL)));
    }
  }
};

// Forward-only closure: forwards alive oops but does NOT null dead oops.
// Used during fix_oop_relocations when class unloading is pending, so that
// has_dead_oop() can still detect nmethods that should be unloaded.
class MMTkForwardOnlyClosure : public OopClosure {
 public:
  inline virtual void do_oop(oop* slot) override {
    const auto o = *slot;
    if (o == NULL) return;
    const auto status = MMTkForwardClosure::read_forwarding_word(o);
    if (MMTkForwardClosure::is_forwarded(status)) {
      *slot = MMTkForwardClosure::extract_forwarding_pointer(status);
    }
  }
  inline virtual void do_oop(narrowOop* slot) override {
    narrowOop heap_oop = RawAccess<>::oop_load(slot);
    if (CompressedOops::is_null(heap_oop)) return;
    oop o = CompressedOops::decode_not_null(heap_oop);
    const auto status = MMTkForwardClosure::read_forwarding_word(o);
    if (MMTkForwardClosure::is_forwarded(status)) {
      RawAccess<>::oop_store(slot, CompressedOops::encode_not_null(MMTkForwardClosure::extract_forwarding_pointer(status)));
    }
  }
};

class MMTkUpdateClosure : public OopClosure {
 public:
  inline virtual void do_oop(oop* slot) override {
    const auto o = *slot;
    const uintptr_t v = uintptr_t((void*) o);
    if (o == NULL) {
      return;
    }
    if (mmtk_is_live((void*) o) == 0) {
      *slot = NULL;
    } else if (MMTkForwardClosure::is_forwarded(MMTkForwardClosure::read_forwarding_word(o))) {
      *slot = MMTkForwardClosure::extract_forwarding_pointer(MMTkForwardClosure::read_forwarding_word(o));
    }
  }
  inline virtual void do_oop(narrowOop* slot) override {
    guarantee(false, "unreachable");
  }
};

extern MaybeUninit<OopStorageSetStrongParState<false, false>> oop_storage_set_strong_par_state;
extern MaybeUninit<OopStorageSetWeakParState<false, false>> oop_storage_set_weak_par_state;

static void mmtk_stop_all_mutators(void *tls, MutatorClosure closure, bool current_gc_should_unload_classes) {
  log_debug(gc)("Requesting the VM to suspend all mutators...");
  MMTkHeap::heap()->companion_thread()->request(MMTkVMCompanionThread::_threads_suspended, true);
  log_debug(gc)("Mutators stopped. Now enumerate threads for scanning...");
  MMTkHeap::heap()->set_is_gc_active(true);

  oop_storage_set_strong_par_state.init();
  oop_storage_set_weak_par_state.init();

  mmtk_report_gc_start();
  if (ClassUnloading && current_gc_should_unload_classes) {
    ClassLoaderDataGraph::clear_claimed_marks();
  }
  // CodeCache::gc_prologue();
#if COMPILER2_OR_JVMCI
  DerivedPointerTable::clear();
#endif

  JavaThreadIteratorWithHandle jtiwh;
  while (JavaThread *cur = jtiwh.next()) {
    closure.invoke((void*)&cur->third_party_heap_mutator);
  }

  log_debug(gc)("Finished enumerating threads.");
  nmethod::oops_do_marking_prologue();
}

static void mmtk_clear_claimed_marks() {
  ClassLoaderDataGraph::clear_claimed_marks();
}

static void mmtk_update_weak_processor(bool lxr) {
  // HandleMark hm(THREAD);
  if (lxr) {
    MMTkLXRFastUpdateClosure cl;
    WeakProcessor::oops_do(&cl);
  } else {
    MMTkUpdateClosure cl;
    WeakProcessor::oops_do(&cl);
  }
}

static void mmtk_oops_do_marking_epilogue() {
  nmethod::oops_do_marking_epilogue();
}

static void mmtk_unload_classes() {
  if (ClassUnloading) {
    // Unload classes and purge SystemDictionary.
    // LOG_CLS_UNLOAD("[mmtk_unload_classes] SystemDictionary::do_unloading");
    // auto purged_classes = SystemDictionary::do_unloading(NULL);
    ClassUnloadingContext ctx(MMTkHeap::heap()->workers()->active_workers(),
                            true /* unregister_nmethods_during_purge */,
                            false /* lock_codeblob_free_separately */);
    MMTkIsAliveClosure is_alive;
    MMTkForwardClosure forward;
    {
      CodeCache::UnlinkingScope scope(&is_alive);
      bool unloading_occurred = SystemDictionary::do_unloading(NULL);
      MMTkHeap::heap()->complete_cleaning(&is_alive, &forward, unloading_occurred);
    }
    // LOG_CLS_UNLOAD("[mmtk_unload_classes] forward code cache ptrs");
    // CodeBlobToOopClosure cb_cl(&forward, true);
    // CodeCache::blobs_do(&cb_cl);
    // LOG_CLS_UNLOAD("[mmtk_unload_classes] complete_cleaning");
    // MMTkHeap::heap()->complete_cleaning(&is_alive, &forward, purged_classes);
    LOG_CLS_UNLOAD("[mmtk_unload_classes] ClassLoaderDataGraph::purge");

    ctx.purge_nmethods();
    // MMTkHeap::heap()->bulk_unregister_nmethods();
    ctx.free_code_blobs();
    ClassLoaderDataGraph::purge(true /* at_safepoint */);
    DEBUG_ONLY(MetaspaceUtils::verify();)
    LOG_CLS_UNLOAD("[mmtk_unload_classes] compute_new_size");
    // Resize and verify metaspace
    MetaspaceGC::compute_new_size();
    // MetaspaceUtils::verify_metrics();
    LOG_CLS_UNLOAD("[mmtk_unload_classes] end");
  }
}

static void mmtk_gc_epilogue() {
  Universe::heap()->update_capacity_and_used_at_gc();
  // CodeCache::gc_epilogue();
  // JvmtiExport::gc_epilogue();
  // ClassLoaderDataGraph::purge();
#if COMPILER2_OR_JVMCI
  DerivedPointerTable::update_pointers();
#endif
  CodeCache::arm_all_nmethods();
}

static void mmtk_resume_mutators(void *tls) {
  // Note: we don't have to hold gc_lock to increment the counter.
  // The increment has to be done before mutators can be resumed (from `block_for_gc` or yieldpoints).
  // Otherwise, mutators might see an outdated start-the-world count.
  Atomic::inc(&mmtk_start_the_world_count);
  log_debug(gc)("Incremented start_the_world counter to %zu.", Atomic::load(&mmtk_start_the_world_count));

  MMTkHeap::heap()->set_is_gc_active(false);
  log_debug(gc)("Requesting the companion thread to resume all mutators blocking on yieldpoints...");
  MMTkHeap::heap()->companion_thread()->request(MMTkVMCompanionThread::_threads_resumed, true);

  log_debug(gc)("Notifying mutators blocking on the start-the-world counter...");
  {
    MutexLocker locker(MMTkHeap::heap()->gc_lock(), Mutex::_no_safepoint_check_flag);
    MMTkHeap::heap()->gc_lock()->notify_all();
  }

  log_debug(gc)("Notifying mutators blocking on Heap_lock for reference pending list...");
  // Note: That's the ReferenceHandler thread.
  {
    MutexLocker x(Heap_lock, Mutex::_no_safepoint_check_flag);
    if (Universe::has_reference_pending_list()) {
      Heap_lock->notify_all();
    }
  }
}

static const int GC_THREAD_KIND_WORKER = 1;
static void mmtk_spawn_gc_thread(void* tls, int kind, void* ctx) {
  switch (kind) {
    case GC_THREAD_KIND_WORKER: {
      MMTkHeap::heap()->new_collector_thread();
      MMTkCollectorThread* t = new MMTkCollectorThread(ctx);
      if (!os::create_thread(t, os::gc_thread)) {
        printf("Failed to create thread");
        guarantee(false, "panic");
      }
      os::start_thread(t);
      break;
    }
    default: {
      printf("Unexpected thread kind: %d\n", kind);
      guarantee(false, "panic");
    }
  }
}

static void mmtk_block_for_gc() {
  MMTkHeap::heap()->_last_gc_time = os::javaTimeNanos() / NANOSECS_PER_MILLISEC;

  // We must read the counter before entering safepoint.
  // This thread (or another mutator) has just triggered GC.
  // The GC cannot start until all mutators enter safepoint.
  // If the GC cannot start, it cannot finish, and cannot increment mmtk_start_the_world_count.
  // Otherwise, if we enter safepoint before reading mmtk_start_the_world_count,
  // we will allow the GC to start before we read the counter,
  // and the GC workers may run so fast that they have finished one whole GC and incremented the
  // counter before this mutator reads the counter for the first time.
  // Once that happens, the current mutator will wait for the next GC forever,
  // which may not happen at all before the program exits.
  size_t my_count = Atomic::load(&mmtk_start_the_world_count);
  size_t next_count = my_count + 1;

  log_debug(gc)("Will block until the start_the_world counter reaches %zu.", next_count);

  {
    // Enter safepoint.
    JavaThread* thread = JavaThread::current();
    ThreadBlockInVM tbivm(thread);

    // No safepoint check.  We are already in safepoint.
    MutexLocker locker(MMTkHeap::heap()->gc_lock(), Mutex::_no_safepoint_check_flag);

    while (Atomic::load(&mmtk_start_the_world_count) < next_count) {
      // wait() may wake up spuriously, but the authoritative condition for unblocking is
      // mmtk_start_the_world_count being incremented.
      MMTkHeap::heap()->gc_lock()->wait_without_safepoint_check();
    }
  }
  log_debug(gc)("Resumed after GC finished.");
}

static void mmtk_out_of_memory(void* tls, MMTkAllocationError err_kind) {
  switch (err_kind) {
  case HeapOutOfMemory :
    // Note that we have to do nothing for the case that the Java heap is too small. Since mmtk-core already
    // returns a nullptr back to the JVM, it automatically triggers an OOM exception since the JVM checks for
    // OOM every (slowpath) allocation [1]. In fact, if we report and throw an OOM exception here, the VM will
    // complain since a pending exception bit was already set when it was trying to check for OOM [2]. Hence,
    // it is best to let the JVM take care of reporting OOM itself.
    //
    // [1]: https://github.com/mmtk/openjdk/blob/e4dbe9909fa5c21685a20a1bc541fcc3b050dac4/src/hotspot/share/gc/shared/memAllocator.cpp#L83
    // [2]: https://github.com/mmtk/openjdk/blob/e4dbe9909fa5c21685a20a1bc541fcc3b050dac4/src/hotspot/share/gc/shared/memAllocator.cpp#L117
    break;
  case MmapOutOfMemory :
    // Abort the VM immediately due to insufficient system resources.
    vm_exit_out_of_memory(0, OOM_MMAP_ERROR, "MMTk: Unable to acquire more memory from the OS. Out of system resources.");
    break;
  }
}

static void* mmtk_get_mmtk_mutator(void* tls) {
  return (void*) &((Thread*) tls)->third_party_heap_mutator;
}

static bool mmtk_is_mutator(void* tls) {
  if (tls == NULL) return false;
  return ((Thread*) tls)->third_party_heap_collector == NULL;
}

static void mmtk_get_mutators(MutatorClosure closure) {
  JavaThread *thr;
  for (JavaThreadIteratorWithHandle jtiwh; (thr = jtiwh.next());) {
    closure.invoke(&thr->third_party_heap_mutator);
  }
}

static void mmtk_scan_roots_in_all_mutator_threads(SlotsClosure closure) {
  MMTkRootsClosure cl(closure);
  MMTkHeap::heap()->scan_roots_in_all_mutator_threads(cl);
}

static void mmtk_scan_roots_in_mutator_thread(SlotsClosure closure, void* tls) {
  ResourceMark rm;
  JavaThread* thread = (JavaThread*) tls;
  MMTkRootsClosure cl(closure);
  MarkingCodeBlobClosure cb_cl(&cl, false, true);
  thread->oops_do(&cl, &cb_cl);
}

static void mmtk_scan_multiple_thread_roots(SlotsClosure closure, void* ptr, size_t len) {
  ResourceMark rm;
  auto mutators = (JavaThread**) ptr;
  MMTkRootsClosure cl(closure);
  MarkingCodeBlobClosure cb_cl(&cl, false, true);
  for (size_t i = 0; i < len; i++)
    mutators[i]->oops_do(&cl, &cb_cl);
}

static void mmtk_scan_object(void* trace, void* object, void* tls, bool follow_clds, bool claim_clds) {
  MMTkScanObjectClosure cl(trace, follow_clds, claim_clds);
  ((oop) object)->oop_iterate(&cl);
}

static void mmtk_dump_object(void* object) {
  oop o = (oop) object;

  // o->print();
  o->print_value();
  printf("\n");

  // o->print_address();
}

static size_t mmtk_get_object_size(void* object) {
  oop o = (oop) object;
  // Slow-dispatch only. The fast-path code is moved to rust.
  auto klass = o->klass();
  return klass->oop_size(o) << LogHeapWordSize;
}

static void mmtk_harness_begin() {
  assert(Thread::current()->is_Java_thread(), "Only Java thread can enter vm");

  JavaThread* current = ((JavaThread*) Thread::current());
  ThreadInVMfromNative tiv(current);
  mmtk_harness_begin_impl();
}

static void mmtk_harness_end() {
  assert(Thread::current()->is_Java_thread(), "Only Java thread can leave vm");

  JavaThread* current = ((JavaThread*) Thread::current());
  ThreadInVMfromNative tiv(current);
  mmtk_harness_end_impl();
}

static int offset_of_static_fields() {
  return InstanceMirrorKlass::offset_of_static_fields();
}

static int static_oop_field_count_offset() {
  return java_lang_Class::static_oop_field_count_offset();
}

static size_t compute_klass_mem_layout_checksum() {
  // printf("C++: Klass %ld, InstanceKlass %ld, InstanceRefKlass %ld, InstanceMirrorKlass %ld, InstanceClassLoaderKlass %ld, TypeArrayKlass %ld, ObjArrayKlass %ld, ArrayKlass %ld\n",
  //   sizeof(Klass)
  //   , sizeof(InstanceKlass)
  //   , sizeof(InstanceRefKlass)
  //   , sizeof(InstanceMirrorKlass)
  //   , sizeof(InstanceClassLoaderKlass)
  //   , sizeof(TypeArrayKlass)
  //   , sizeof(ObjArrayKlass)
  //   , sizeof(ArrayKlass)
  // );
  return sizeof(Klass)
    ^ sizeof(InstanceKlass)
    ^ sizeof(InstanceRefKlass)
    ^ sizeof(InstanceMirrorKlass)
    ^ sizeof(InstanceClassLoaderKlass)
    ^ sizeof(TypeArrayKlass)
    ^ sizeof(ObjArrayKlass);
}

static int referent_offset() {
  return java_lang_ref_Reference::referent_offset();
}

static int discovered_offset() {
  return java_lang_ref_Reference::discovered_offset();
}

char data[1024];

static const char* dump_object_string(void* object) {
  // HandleMark hm(THREAD);
  ResourceMark rm;
  if (object == NULL) return NULL;
  oop o = (oop) object;
  const char* c = o->klass()->internal_name();
  strcpy(&data[0], c);
  return &data[0];
}

static void mmtk_schedule_finalizer() {
  MMTkHeap::heap()->schedule_finalizer();
}

static void mmtk_scan_code_cache_roots(SlotsClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_code_cache_roots(cl); }
static void mmtk_scan_class_loader_data_graph_roots(SlotsClosure closure, SlotsClosure weak_closure, bool scan_all_strong_roots) {
  MMTkRootsClosure cl(closure);
  MMTkRootsClosure weak_cl(weak_closure);
  MMTkHeap::heap()->scan_class_loader_data_graph_roots(cl, weak_cl, scan_all_strong_roots);
}
static void mmtk_scan_oop_storage_set_roots(SlotsClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_oop_storage_set_roots(cl); }
static void mmtk_scan_weak_processor_roots(SlotsClosure closure) {
  MMTkRootsClosure cl(closure);
  MMTkHeap::heap()->scan_weak_processor_roots(cl);
}
static void mmtk_scan_vm_thread_roots(SlotsClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_vm_thread_roots(cl); }

static size_t mmtk_number_of_mutators() {
  return Threads::number_of_threads();
}

static void mmtk_prepare_for_roots_re_scanning() {
#if COMPILER2_OR_JVMCI
  DerivedPointerTable::update_pointers();
  DerivedPointerTable::clear();
#endif
}

static void mmtk_enqueue_references(void** objects, size_t len) {
  if (len == 0) {
    return;
  }

  MutexLocker x(Heap_lock);

  oop first = (oop) objects[0]; // This points to the first node of the linked list.
  oop last = first; // This points to the last node of the linked list.

  for (size_t i = 1; i < len; i++) {
    oop reff = (oop) objects[i];

    // Note that the `objects[]` array may contain duplicated elements.
    // References live after the previous collection will remain in the `ReferenceProcessor` in mmtk-core,
    // which live in the from-space in the current collection.
    // When the OpenJDK binding gradually discovers references during transitive closure,
    // it will add new pointers to the reference processor, pointing to `Reference` instances in the to-space.
    // After processing references, all references will be forwarded and become to-space references,
    // but a former from-space pointer may become the same as one of the newly added to-space references,
    // and the mmtk-core does not deduplicate them.
    // We need to deduplicate the `objects[]` array.

    if (reff == last) {
      // The `discovered` field of `last` is still NULL,
      // but we skip the current `reff` if it happens to be the same as `last`.
      continue;
    }
    oop old_discovered = HeapAccess<AS_RAW>::oop_load_at(reff, java_lang_ref_Reference::discovered_offset());
    if (old_discovered != NULL) {
      // We skip references that already have the `discovered` field set because they have already been visited.
      continue;
    }
    HeapAccess<AS_RAW>::oop_store_at(last, java_lang_ref_Reference::discovered_offset(), reff);
    last = reff;
  }

  oop old_first = Universe::swap_reference_pending_list(first);
  HeapAccess<AS_RAW>::oop_store_at(last, java_lang_ref_Reference::discovered_offset(), old_first);
  assert(Universe::has_reference_pending_list(), "Reference pending list is empty after swap");
}

static void* mmtk_swap_reference_pending_list(void* object) {
  return Universe::swap_reference_pending_list((oop) object);
}

static size_t mmtk_java_lang_class_klass_offset_in_bytes() {
  auto v = java_lang_Class::klass_offset();
  guarantee(v != 0 && v != -1, "checking");
  return v;
}

static size_t mmtk_java_lang_classloader_loader_data_offset() {
  auto v = java_lang_ClassLoader::loader_data_offset();
  guarantee(v != 0 && v != -1, "checking");
  return v;
}

void mmtk_fix_oop_relocations(bool lxr, bool forward_only, void* nmethods, size_t len) {
  if (forward_only) {
    // Forward-only: forward alive oops but don't null dead oops.
    // Used when class unloading is pending so has_dead_oop() can detect dead nmethods.
    MMTkForwardOnlyClosure cl;
    for (size_t i = 0; i < len; i++) {
      nmethod* nm = (nmethod*) ((nmethod**) nmethods)[i];
      nm->oops_do(&cl);
      nm->fix_oop_relocations();
    }
  } else if (lxr) {
    MMTkLXRFastUpdateClosure cl;
    for (size_t i = 0; i < len; i++) {
      nmethod* nm = (nmethod*) ((nmethod**) nmethods)[i];
      nm->oops_do(&cl);
      nm->fix_oop_relocations();
    }
  } else {
    MMTkUpdateClosure cl;
    for (size_t i = 0; i < len; i++) {
      nmethod* nm = (nmethod*) ((nmethod**) nmethods)[i];
      nm->oops_do(&cl);
      nm->fix_oop_relocations();
    }
  }
}

OpenJDK_Upcalls mmtk_upcalls = {
  mmtk_stop_all_mutators,
  mmtk_resume_mutators,
  mmtk_spawn_gc_thread,
  mmtk_block_for_gc,
  mmtk_out_of_memory,
  mmtk_get_mutators,
  mmtk_scan_object,
  mmtk_dump_object,
  mmtk_get_object_size,
  mmtk_get_mmtk_mutator,
  mmtk_is_mutator,
  mmtk_harness_begin,
  mmtk_harness_end,
  compute_klass_mem_layout_checksum,
  offset_of_static_fields,
  static_oop_field_count_offset,
  referent_offset,
  discovered_offset,
  dump_object_string,
  mmtk_scan_roots_in_all_mutator_threads,
  mmtk_scan_roots_in_mutator_thread,
  mmtk_scan_multiple_thread_roots,
  mmtk_scan_code_cache_roots,
  mmtk_scan_class_loader_data_graph_roots,
  mmtk_scan_oop_storage_set_roots,
  mmtk_scan_weak_processor_roots,
  mmtk_scan_vm_thread_roots,
  mmtk_number_of_mutators,
  mmtk_schedule_finalizer,
  mmtk_prepare_for_roots_re_scanning,
  mmtk_update_weak_processor,
  mmtk_enqueue_references,
  mmtk_swap_reference_pending_list,
  mmtk_java_lang_class_klass_offset_in_bytes,
  mmtk_java_lang_classloader_loader_data_offset,
  mmtk_fix_oop_relocations,
  mmtk_clear_claimed_marks,
  mmtk_unload_classes,
  mmtk_gc_epilogue,
  mmtk_oops_do_marking_epilogue,
};
