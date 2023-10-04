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

#include "precompiled.hpp"
#include "classfile/stringTable.hpp"
#include "code/nmethod.hpp"
#include "memory/iterator.inline.hpp"
#include "memory/resourceArea.hpp"
#include "mmtkCollectorThread.hpp"
#include "mmtkContextThread.hpp"
#include "mmtkHeap.hpp"
#include "mmtkRootsClosure.hpp"
#include "mmtkUpcalls.hpp"
#include "mmtkVMCompanionThread.hpp"
#include "runtime/atomic.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/thread.hpp"
#include "runtime/threadSMR.hpp"
#include "runtime/vmThread.hpp"
#include "utilities/debug.hpp"

// Note: This counter must be accessed using the Atomic class.
static volatile size_t mmtk_start_the_world_count = 0;

static void mmtk_stop_all_mutators(void *tls, MutatorClosure closure) {
  ClassLoaderDataGraph::clear_claimed_marks();
  CodeCache::gc_prologue();
#if COMPILER2_OR_JVMCI
  DerivedPointerTable::clear();
#endif

  log_debug(gc)("Requesting the VM to suspend all mutators...");
  MMTkHeap::heap()->companion_thread()->request(MMTkVMCompanionThread::_threads_suspended, true);
  log_debug(gc)("Mutators stopped. Now enumerate threads for scanning...");

  JavaThreadIteratorWithHandle jtiwh;
  while (JavaThread *cur = jtiwh.next()) {
    closure.invoke((void*)&cur->third_party_heap_mutator);
  }

  log_debug(gc)("Finished enumerating threads.");
  nmethod::oops_do_marking_prologue();
}

static void mmtk_resume_mutators(void *tls) {
  nmethod::oops_do_marking_epilogue();
  // ClassLoaderDataGraph::purge();
  CodeCache::gc_epilogue();
  JvmtiExport::gc_epilogue();
#if COMPILER2_OR_JVMCI
  DerivedPointerTable::update_pointers();
#endif

  // Note: we don't have to hold gc_lock to increment the counter.
  // The increment has to be done before mutators can be resumed
  // otherwise, mutators might see a stale value
  Atomic::inc(&mmtk_start_the_world_count);

  log_debug(gc)("Requesting the VM to resume all mutators...");
  MMTkHeap::heap()->companion_thread()->request(MMTkVMCompanionThread::_threads_resumed, true);
  log_debug(gc)("Mutators resumed. Now notify any mutators waiting for GC to finish...");

  {
    MutexLockerEx locker(MMTkHeap::heap()->gc_lock(), true);
    MMTkHeap::heap()->gc_lock()->notify_all();
  }
  log_debug(gc)("Mutators notified.");
}

static const int GC_THREAD_KIND_CONTROLLER = 0;
static const int GC_THREAD_KIND_WORKER = 1;
static void mmtk_spawn_gc_thread(void* tls, int kind, void* ctx) {
  switch (kind) {
    case GC_THREAD_KIND_CONTROLLER: {
      MMTkContextThread* t = new MMTkContextThread(ctx);
      if (!os::create_thread(t, os::pgc_thread)) {
        printf("Failed to create thread");
        guarantee(false, "panic");
      }
      os::start_thread(t);
      break;
    }
    case GC_THREAD_KIND_WORKER: {
      MMTkHeap::heap()->new_collector_thread();
      MMTkCollectorThread* t = new MMTkCollectorThread(ctx);
      if (!os::create_thread(t, os::pgc_thread)) {
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
  log_debug(gc)("Thread (id=%d) will block waiting for GC to finish.", Thread::current()->osthread()->thread_id());

  // We must read the counter before entering safepoint.
  // This thread has just triggered GC.
  // Before this thread enters safe point, the GC cannot start, and therefore cannot finish,
  // and cannot increment the counter mmtk_start_the_world_count.
  // Otherwise, if we attempt to acquire the gc_lock first, GC may have triggered stop-the-world
  // first, and this thread will be blocked for the entire stop-the-world duration before it can
  // get the lock.  Once that happens, the current thread will read the counter after the GC, and
  // wait for the next non-existing GC forever.
  size_t my_count = Atomic::load(&mmtk_start_the_world_count);
  size_t next_count = my_count + 1;

  {
    // Once this thread acquires the lock, the VM will consider this thread to be "in safe point".
    MutexLocker locker(MMTkHeap::heap()->gc_lock());

    while (Atomic::load(&mmtk_start_the_world_count) < next_count) {
      // wait() may wake up spuriously, but the authoritative condition for unblocking is
      // mmtk_start_the_world_count being incremented.
      MMTkHeap::heap()->gc_lock()->wait();
    }
  }
  log_debug(gc)("Thread (id=%d) resumed after GC finished.", Thread::current()->osthread()->thread_id());
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
  for (JavaThreadIteratorWithHandle jtiwh; thr = jtiwh.next();) {
    closure.invoke(&thr->third_party_heap_mutator);
  }
}

static void mmtk_scan_roots_in_all_mutator_threads(EdgesClosure closure) {
  MMTkRootsClosure cl(closure);
  MMTkHeap::heap()->scan_roots_in_all_mutator_threads(cl);
}

static void mmtk_scan_roots_in_mutator_thread(EdgesClosure closure, void* tls) {
  ResourceMark rm;
  JavaThread* thread = (JavaThread*) tls;
  MMTkRootsClosure cl(closure);
  thread->oops_do(&cl, NULL);
}

static void mmtk_scan_object(void* trace, void* object, void* tls) {
  MMTkScanObjectClosure cl(trace);
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
  return sizeof(Klass)
    ^ sizeof(InstanceKlass)
    ^ sizeof(InstanceRefKlass)
    ^ sizeof(InstanceMirrorKlass)
    ^ sizeof(InstanceClassLoaderKlass)
    ^ sizeof(TypeArrayKlass)
    ^ sizeof(ObjArrayKlass);
}

static int referent_offset() {
  return java_lang_ref_Reference::referent_offset;
}

static int discovered_offset() {
  return java_lang_ref_Reference::discovered_offset;
}

static char* dump_object_string(void* object) {
  oop o = (oop) object;
  return o->print_value_string();
}

static void mmtk_schedule_finalizer() {
  MMTkHeap::heap()->schedule_finalizer();
}

static void mmtk_scan_universe_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_universe_roots(cl); }
static void mmtk_scan_jni_handle_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_jni_handle_roots(cl); }
static void mmtk_scan_object_synchronizer_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_object_synchronizer_roots(cl); }
static void mmtk_scan_management_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_management_roots(cl); }
static void mmtk_scan_jvmti_export_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_jvmti_export_roots(cl); }
static void mmtk_scan_aot_loader_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_aot_loader_roots(cl); }
static void mmtk_scan_system_dictionary_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_system_dictionary_roots(cl); }
static void mmtk_scan_code_cache_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_code_cache_roots(cl); }
static void mmtk_scan_string_table_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_string_table_roots(cl); }
static void mmtk_scan_class_loader_data_graph_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_class_loader_data_graph_roots(cl); }
static void mmtk_scan_weak_processor_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_weak_processor_roots(cl); }
static void mmtk_scan_vm_thread_roots(EdgesClosure closure) { MMTkRootsClosure cl(closure); MMTkHeap::heap()->scan_vm_thread_roots(cl); }

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

  oop prev = NULL;
  for (size_t i = 0; i < len; i++) {
    oop reff = (oop) objects[i];
    if (prev != NULL) {
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(prev, java_lang_ref_Reference::discovered_offset, reff);
    }
    prev = reff;
  }

  oop old = Universe::swap_reference_pending_list(prev);
  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(prev, java_lang_ref_Reference::discovered_offset, old);
  assert(Universe::has_reference_pending_list(), "Reference pending list is empty after swap");
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
  mmtk_scan_universe_roots,
  mmtk_scan_jni_handle_roots,
  mmtk_scan_object_synchronizer_roots,
  mmtk_scan_management_roots,
  mmtk_scan_jvmti_export_roots,
  mmtk_scan_aot_loader_roots,
  mmtk_scan_system_dictionary_roots,
  mmtk_scan_code_cache_roots,
  mmtk_scan_string_table_roots,
  mmtk_scan_class_loader_data_graph_roots,
  mmtk_scan_weak_processor_roots,
  mmtk_scan_vm_thread_roots,
  mmtk_number_of_mutators,
  mmtk_schedule_finalizer,
  mmtk_prepare_for_roots_re_scanning,
  mmtk_enqueue_references
};
