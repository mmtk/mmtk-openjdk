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

/* 
 * File:   mmtkUpcalls.cpp
 * Author: Pavel Zakopaylo
 *
 * Created on 30 November 2018, 5:45 PM
 */

#include "mmtkUpcalls.hpp"
#include "mmtkRootsClosure.hpp"
#include "mmtkHeap.hpp"
#include "mmtkContextThread.hpp"
#include "mmtkCollectorThread.hpp"
#include "runtime/os.hpp"
#include "runtime/vmThread.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"
#include "runtime/threadSMR.hpp"
#include "classfile/stringTable.hpp"
#include "code/nmethod.hpp"

static bool gcInProgress = false;

static void mmtk_stop_all_mutators(void *tls) {
    SafepointSynchronize::begin();
    gcInProgress = true;
    
}

static void mmtk_resume_mutators(void *tls) {
    SafepointSynchronize::end();
    MMTkHeap::heap()->gc_lock()->lock_without_safepoint_check();
    gcInProgress = false;
    MMTkHeap::heap()->gc_lock()->notify_all();
    MMTkHeap::heap()->gc_lock()->unlock();
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
    do {
        MMTkHeap::heap()->gc_lock()->lock();
        MMTkHeap::heap()->gc_lock()->wait();
        MMTkHeap::heap()->gc_lock()->unlock();
    } while (gcInProgress);
}

static void* mmtk_active_collector(void* tls) {
    return ((MMTkCollectorThread*) tls)->get_context();
}

static void* mmtk_get_mmtk_mutator(void* tls) {
    return (void*) ((Thread*) tls)->mmtk_mutator();
}

static bool mmtk_is_mutator(void* tls) {
    return ((Thread*) tls)->mmtk_collector() == NULL;
}

static JavaThread* _thread_cursor = NULL;

static void* mmtk_get_next_mutator() {
    if (_thread_cursor == NULL) {
        _thread_cursor = Threads::get_thread_list();
    } else {
        _thread_cursor = _thread_cursor->next();
    }
    // printf("_thread_cursor %p -> %p\n", _thread_cursor, _thread_cursor == NULL ? NULL : _thread_cursor->mmtk_mutator());
    if (_thread_cursor == NULL) return NULL;
    return (void*) _thread_cursor->mmtk_mutator();
}

static void mmtk_reset_mutator_iterator() {
    _thread_cursor = NULL;
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

// static void mmtk_start_computing_roots() {
//     nmethod::oops_do_marking_prologue();
//     Threads::change_thread_claim_parity();
//     // Zero the claimed high water mark in the StringTable
//     StringTable::clear_parallel_claimed_index();
// }

// static void mmtk_finish_computing_roots() {
//   nmethod::oops_do_marking_epilogue();
//   Threads::assert_all_threads_claimed();
// }

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

OpenJDK_Upcalls mmtk_upcalls = {
    mmtk_stop_all_mutators,
    mmtk_resume_mutators,
    mmtk_spawn_collector_thread,
    mmtk_block_for_gc,
    mmtk_active_collector,
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
};