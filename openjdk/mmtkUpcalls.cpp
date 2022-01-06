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
#include "classfile/classLoaderDataGraph.hpp"
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
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/thread.hpp"
#include "runtime/threadSMR.hpp"
#include "runtime/vmThread.hpp"

static size_t mmtk_start_the_world_count = 0;

static void mmtk_stop_all_mutators(void *tls, void (*create_stack_scan_work)(void* mutator)) {
  MMTkHeap::_create_stack_scan_work = create_stack_scan_work;

  ClassLoaderDataGraph::clear_claimed_marks();
#if COMPILER2_OR_JVMCI
  DerivedPointerTable::clear();
#endif

  log_debug(gc)("Requesting the VM to suspend all mutators...");
  MMTkHeap::heap()->companion_thread()->request(MMTkVMCompanionThread::_threads_suspended, true);
  log_debug(gc)("Mutators stopped. Now enumerate threads for scanning...");

  nmethod::oops_do_marking_prologue();
  {
    JavaThreadIteratorWithHandle jtiwh;
    while (JavaThread *cur = jtiwh.next()) {
      MMTkHeap::heap()->report_java_thread_yield(cur);
    }
  }
  log_debug(gc)("Finished enumerating threads.");
}

static void mmtk_resume_mutators(void *tls) {
  ClassLoaderDataGraph::purge(true);
  nmethod::oops_do_marking_epilogue();
#if COMPILER2_OR_JVMCI
  DerivedPointerTable::update_pointers();
#endif

  MMTkHeap::_create_stack_scan_work = NULL;

  log_debug(gc)("Requesting the VM to resume all mutators...");
  MMTkHeap::heap()->companion_thread()->request(MMTkVMCompanionThread::_threads_resumed, true);
  log_debug(gc)("Mutators resumed. Now notify any mutators waiting for GC to finish...");

  {
    MutexLocker locker(MMTkHeap::heap()->gc_lock());
    mmtk_start_the_world_count++;
    MMTkHeap::heap()->gc_lock()->notify_all();
  }
  log_debug(gc)("Mutators notified.");
}

static void mmtk_spawn_collector_thread(void* tls, void* ctx) {
  if (ctx == NULL) {
    MMTkContextThread* t = new MMTkContextThread();
    if (!os::create_thread(t, os::pgc_thread)) {
      printf("Failed to create thread");
      guarantee(false, "panic");
    }
    os::start_thread(t);
  } else {
    MMTkHeap::heap()->new_collector_thread();
    MMTkCollectorThread* t = new MMTkCollectorThread(ctx);
    if (!os::create_thread(t, os::pgc_thread)) {
      printf("Failed to create thread");
      guarantee(false, "panic");
    }
    os::start_thread(t);
  }
}

static void mmtk_block_for_gc() {
  MMTkHeap::heap()->_last_gc_time = os::javaTimeNanos() / NANOSECS_PER_MILLISEC;
  log_debug(gc)("Thread (id=%d) will block waiting for GC to finish.", Thread::current()->osthread()->thread_id());
  {
    MutexLocker locker(MMTkHeap::heap()->gc_lock());
    size_t my_count = mmtk_start_the_world_count;
    size_t next_count = my_count + 1;

    while (mmtk_start_the_world_count < next_count) {
      MMTkHeap::heap()->gc_lock()->wait();
    }
  }
  log_debug(gc)("Thread (id=%d) resumed after GC finished.", Thread::current()->osthread()->thread_id());
}

static void* mmtk_get_mmtk_mutator(void* tls) {
  return (void*) &((Thread*) tls)->third_party_heap_mutator;
}

static bool mmtk_is_mutator(void* tls) {
  if (tls == NULL) return false;
  return ((Thread*) tls)->third_party_heap_collector == NULL;
}

template <class T>
struct MaybeUninit {
  MaybeUninit() {}
  T* operator->() {
    return (T*) &_data;
  }
  T& operator*() {
    return *((T*) &_data);
  }
private:
  char _data[sizeof(T)];
};

static MaybeUninit<JavaThreadIteratorWithHandle> jtiwh;
static bool mutator_iteration_start = true;

static void* mmtk_get_next_mutator() {
  if (mutator_iteration_start) {
    *jtiwh = JavaThreadIteratorWithHandle();
    mutator_iteration_start = false;
  }
  JavaThread *thr = jtiwh->next();
  if (thr == NULL) {
    mutator_iteration_start = true;
    return NULL;
  }
  return (void*) &thr->third_party_heap_mutator;
}

static void mmtk_reset_mutator_iterator() {
  mutator_iteration_start = true;
}


static void mmtk_compute_global_roots(void* trace, void* tls) {
  MMTkRootsClosure cl(trace);
  MMTkHeap::heap()->scan_global_roots(cl);
}

static void mmtk_compute_static_roots(void* trace, void* tls) {
  MMTkRootsClosure cl(trace);
  MMTkHeap::heap()->scan_static_roots(cl);
}

static void mmtk_compute_thread_roots(void* trace, void* tls) {
  MMTkRootsClosure cl(trace);
  MMTkHeap::heap()->scan_thread_roots(cl);
}

static void mmtk_scan_thread_roots(ProcessEdgesFn process_edges) {
  MMTkRootsClosure2 cl(process_edges);
  MMTkHeap::heap()->scan_thread_roots(cl);
}

static void mmtk_scan_thread_root(ProcessEdgesFn process_edges, void* tls) {
  ResourceMark rm;
  JavaThread* thread = (JavaThread*) tls;
  MMTkRootsClosure2 cl(process_edges);
  MarkingCodeBlobClosure cb_cl(&cl, false);
  thread->oops_do(&cl, &cb_cl);
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
  return o->size() * HeapWordSize;
}

static int mmtk_enter_vm() {
  assert(Thread::current()->is_Java_thread(), "Only Java thread can enter vm");

  JavaThread* current = ((JavaThread*) Thread::current());
  JavaThreadState state = current->thread_state();
  current->set_thread_state(_thread_in_vm);
  return (int)state;
}

static void mmtk_leave_vm(int st) {
  assert(Thread::current()->is_Java_thread(), "Only Java thread can leave vm");

  JavaThread* current = ((JavaThread*) Thread::current());
  assert(current->thread_state() == _thread_in_vm, "Cannot leave vm when the current thread is not in _thread_in_vm");
  current->set_thread_state((JavaThreadState)st);
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
  return java_lang_ref_Reference::referent_offset();
}

static int discovered_offset() {
  return java_lang_ref_Reference::discovered_offset();
}

static char* dump_object_string(void* object) {
  oop o = (oop) object;
  return o->print_value_string();
}

static void mmtk_schedule_finalizer() {
  MMTkHeap::heap()->schedule_finalizer();
}

static void mmtk_scan_jni_handle_roots(ProcessEdgesFn process_edges) { MMTkRootsClosure2 cl(process_edges); MMTkHeap::heap()->scan_jni_handle_roots(cl); }
static void mmtk_scan_vm_global_roots(ProcessEdgesFn process_edges) { MMTkRootsClosure2 cl(process_edges); MMTkHeap::heap()->scan_vm_global_roots(cl); }
static void mmtk_scan_code_cache_roots(ProcessEdgesFn process_edges) { MMTkRootsClosure2 cl(process_edges); MMTkHeap::heap()->scan_code_cache_roots(cl); }
static void mmtk_scan_class_loader_data_graph_roots(ProcessEdgesFn process_edges) { MMTkRootsClosure2 cl(process_edges); MMTkHeap::heap()->scan_class_loader_data_graph_roots(cl); }
static void mmtk_scan_weak_processor_roots(ProcessEdgesFn process_edges) { MMTkRootsClosure2 cl(process_edges); MMTkHeap::heap()->scan_weak_processor_roots(cl); }
static void mmtk_scan_vm_thread_roots(ProcessEdgesFn process_edges) { MMTkRootsClosure2 cl(process_edges); MMTkHeap::heap()->scan_vm_thread_roots(cl); }

static size_t mmtk_number_of_mutators() {
  return Threads::number_of_threads();
}

static void mmtk_prepare_for_roots_re_scanning() {
#if COMPILER2_OR_JVMCI
  DerivedPointerTable::update_pointers();
  DerivedPointerTable::clear();
#endif
}

OpenJDK_Upcalls mmtk_upcalls = {
  mmtk_stop_all_mutators,
  mmtk_resume_mutators,
  mmtk_spawn_collector_thread,
  mmtk_block_for_gc,
  mmtk_get_next_mutator,
  mmtk_reset_mutator_iterator,
  mmtk_compute_static_roots,
  mmtk_compute_global_roots,
  mmtk_compute_thread_roots,
  mmtk_scan_object,
  mmtk_dump_object,
  mmtk_get_object_size,
  mmtk_get_mmtk_mutator,
  mmtk_is_mutator,
  mmtk_enter_vm,
  mmtk_leave_vm,
  compute_klass_mem_layout_checksum,
  offset_of_static_fields,
  static_oop_field_count_offset,
  referent_offset,
  discovered_offset,
  dump_object_string,
  mmtk_scan_thread_roots,
  mmtk_scan_thread_root,
  mmtk_scan_jni_handle_roots,
  mmtk_scan_vm_global_roots,
  mmtk_scan_code_cache_roots,
  mmtk_scan_class_loader_data_graph_roots,
  mmtk_scan_weak_processor_roots,
  mmtk_scan_vm_thread_roots,
  mmtk_number_of_mutators,
  mmtk_schedule_finalizer,
  mmtk_prepare_for_roots_re_scanning,
};
