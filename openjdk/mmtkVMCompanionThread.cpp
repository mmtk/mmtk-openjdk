/*
 * Copyright (c) 1998, 2017, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "mmtk.h"
#include "mmtkVMCompanionThread.hpp"
#include "runtime/mutex.hpp"
#include "logging/log.hpp"

MMTkVMCompanionThread::MMTkVMCompanionThread():
    NamedThread(),
    _desired_state(_threads_resumed),
    _reached_state(_threads_resumed) {
  set_name("MMTK VM Companion Thread");
  _lock = new Monitor(Monitor::nonleaf,
                      "MMTkVMCompanionThread::_lock",
                      true,
                      Monitor::_safepoint_check_never);
}

MMTkVMCompanionThread::~MMTkVMCompanionThread() {
  guarantee(false, "MMTkVMCompanionThread deletion must fix the race with VM termination");
}

void MMTkVMCompanionThread::run() {
  this->initialize_named_thread();

  for (;;) {
    // Wait for suspend request
    log_trace(gc)("MMTkVMCompanionThread: Waiting for suspend request...");
    {
      MutexLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
      assert(_reached_state == _threads_resumed, "Threads should be running at this moment.");
      while (_desired_state != _threads_suspended) {
        _lock->wait(Mutex::_no_safepoint_check_flag);
      }
      assert(_reached_state == _threads_resumed, "Threads should still be running at this moment.");
    }

    // Let the VM thread stop the world.
    log_trace(gc)("MMTkVMCompanionThread: Letting VMThread execute VM op...");
    VM_MMTkSTWOperation op(this);
    // VMThread::execute() is blocking. The companion thread will be blocked
    // here waiting for the VM thread to execute op, and the VM thread will
    // be blocked in do_mmtk_stw_operation() until a GC thread
    // calls request(_threads_resumed).
    VMThread::execute(&op);
  }
}

// Request stop-the-world or start-the-world.  This method is supposed to be
// called by a GC thread.
//
// If wait_until_reached is true, the caller will block until all Java threads
// have stopped, or until they have been waken up.
//
// If wait_until_reached is false, the caller will return immediately, while
// the companion thread will ask the VM thread to perform the state transition
// in the background. The caller may call the wait_for_reached method to block
// until the desired state is reached.
void MMTkVMCompanionThread::request(stw_state desired_state, bool wait_until_reached) {
  assert(!Thread::current()->is_VM_thread(), "Requests can only be made by GC threads. Found VM thread.");
  assert(Thread::current() != this, "Requests can only be made by GC threads. Found companion thread.");
  assert(!Thread::current()->is_Java_thread(), "Requests can only be made by GC threads. Found Java thread.");

  MutexLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  assert(_desired_state != desired_state, "State %d already requested.", desired_state);
  _desired_state = desired_state;
  _lock->notify_all();

  if (wait_until_reached) {
    while (_reached_state != desired_state) {
      _lock->wait(Mutex::_no_safepoint_check_flag);
    }
  }
}

// Wait until the desired state is reached.  Usually called after calling the
// request method.  Supposed to be called by a GC thread.
void MMTkVMCompanionThread::wait_for_reached(stw_state desired_state) {
  assert(!Thread::current()->is_VM_thread(), "Supposed to be called by GC threads. Found VM thread.");
  assert(Thread::current() != this, "Supposed to be called by GC threads. Found companion thread.");
  assert(!Thread::current()->is_Java_thread(), "Supposed to be called by GC threads. Found Java thread.");

  MutexLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  assert(_desired_state == desired_state, "State %d not requested.", desired_state);

  while (_reached_state != desired_state) {
    _lock->wait(Mutex::_no_safepoint_check_flag);
  }
}

// Called by the VM thread in `VM_MMTkSTWOperation`.
// This method notify that all Java threads have yielded, and will block the VM thread (thereby
// blocking Java threads) until the GC requests start-the-world.
void MMTkVMCompanionThread::do_mmtk_stw_operation() {
  assert(Thread::current()->is_VM_thread(), "do_mmtk_stw_operation can only be executed by the VM thread");

  {
    MutexLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);

    // Tell the waiter thread that Java threads have stopped at yieldpoints.
    _reached_state = _threads_suspended;
    log_trace(gc)("do_mmtk_stw_operation: Reached _thread_suspended state. Notifying...");
    _lock->notify_all();

    // Wait until resume-the-world is requested
    while (_desired_state != _threads_resumed) {
      _lock->wait(Mutex::_no_safepoint_check_flag);
    }

    // Tell the waiter thread that Java threads will eventually resume from yieldpoints.  This
    // function will return, and, as soon as the VM thread stops executing safepoint VM operations,
    // Java threads will resume from yieldpoints.
    //
    // Note: We have to notify *now* instead of after `VMThread::execute()`.  For reasons unknown
    // (likely a bug in OpenJDK 11), the VMThread fails to notify the companion thread after
    // evaluating `VM_MMTkSTWOperation`, and continues to execute other VM operations (such as
    // `RevokeBias`). This leaves the companion thread blocking on `VMThread::execute()` until the
    // VM thread finishes executing the next batch of queued VM operations.  If we notify after
    // `VMThread::execute` in `run()`, it will cause a deadlock like the following:
    //
    // - The companion thread is blocked at `VMThread::execute()`, waiting for the next batch of VM
    //   operations to finish.
    // - The VM thread is blocked in `SafepointSynchronize::begin()`, waiting for all mutators to
    //   reach safepoints.
    // - One mutator is allocating too fast and triggers a GC, which requires the `WorkerMonitor`
    //   lock in mmtk-core.
    // - A GC worker is still executing `mmtk_resume_mutator`, holding the `WorkerMutator` (as the
    //   last parked GC worker).  It is asking the companion thread to resume mutators, and is still
    //   waiting for the companion thread to reach the `_thread_resumed` state.  As we see before,
    //   the companion thread is waiting, too.
    //
    // By notifying now, we unblock the last parked GC worker before the companion thread returns
    // from `VMThread::execute`, allowing the GC worker to finish `resume_mutators`, breaking the
    // deadlock.  When the next GC starts, the GC worker running `mmtk_stop_all_mutators` will need
    // to wait a little longer (as it always should) until the VM thread finishes executing other VM
    // operations and the companion thread is ready to respond to another request from GC workers.
    //
    // Also note that OpenJDK 17 changed the way the VM thread executes VM operations.  The same
    // problem may not manifest in OpenJDK 17 or 21.
    assert(_desired_state == _threads_resumed, "start-the-world should be requested.");
    assert(_reached_state == _threads_suspended, "Threads should still be suspended at this moment.");
    _reached_state = _threads_resumed;
    log_trace(gc)("do_mmtk_stw_operation: Reached _thread_resumed state. Notifying...");
    _lock->notify_all();
  }
}
