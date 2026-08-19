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
#include "mmtk.h"
#include "mmtkVMCompanionThread.hpp"
#include "mmtkVMOperation.hpp"
#include "interpreter/oopMapCache.hpp"
#include "logging/log.hpp"
#include "interpreter/oopMapCache.hpp"
#include "gc/shared/gcLocker.hpp"

VM_MMTkSTWOperation::VM_MMTkSTWOperation(MMTkVMCompanionThread *companion_thread):
    _companion_thread(companion_thread) {
}

bool VM_MMTkSTWOperation::doit_prologue() {
    Heap_lock->lock();
    return true;
}

void VM_MMTkSTWOperation::doit() {
    if (GCLocker::check_active_before_gc()) {
        // If some threads is in JNI critical region, we don't do a GC for now,
        // and end this VM operation earlier. Under such case, `GCLocker::check_active_before_gc`
        // will remember there is a pending GC. After the thread exits the critical region,
        // if a pending GC needs to be triggered, the java thread will
        // call `MMTkHeap::collect(GCCause::_gc_locker)`.
        // Since we've already have a unfinished GC request inside mmtk,
        // mmtk will not trigger another GC, but simply blocking this thread.
        // After all threads are successfully blocked, the previously
        // triggered pending GC will proceed.
        _companion_thread->_wait_for_gc_locker = true;
        return;
    }
    // JvmtiGCMarker _jgcm;
    log_trace(vmthread)("Entered VM_MMTkSTWOperation::doit().");
    _companion_thread->do_mmtk_stw_operation();
    log_trace(vmthread)("Leaving VM_MMTkSTWOperation::doit()");
}

void VM_MMTkSTWOperation::doit_epilogue() {
    // Clean up old interpreter OopMap entries that were replaced
    // during the GC thread root traversal.
    // OopMapCache::cleanup_old_entries();
    // Notify the reference processing thread
    if (Universe::has_reference_pending_list()) {
        Heap_lock->notify_all();
    }
    Heap_lock->unlock();
}
