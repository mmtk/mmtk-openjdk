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
#include "classfile/stringTable.hpp"
#include "mmtk.h"
#include "mmtkFinalizerThread.hpp"
#include "prims/jvmtiImpl.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/serviceThread.hpp"
#include "services/diagnosticArgument.hpp"
#include "services/diagnosticFramework.hpp"
#include "services/gcNotifier.hpp"
#include "services/lowMemoryDetector.hpp"

MMTkFinalizerThread* MMTkFinalizerThread::instance = NULL;

void MMTkFinalizerThread::initialize() {
  EXCEPTION_MARK;

  HandleMark hm;

  const char* name = "MMTk Finalizer Thread";
  Handle string = java_lang_String::create_from_str(name, CHECK);

  // Initialize thread_oop to put it into the system threadGroup
  Handle thread_group (THREAD, Universe::system_thread_group());
  Handle thread_oop = JavaCalls::construct_new_instance(SystemDictionary::Thread_klass(),
                                                        vmSymbols::threadgroup_string_void_signature(),
                                                        thread_group,
                                                        string,
                                                        CHECK);

  {
    MutexLocker mu(Threads_lock);
    MMTkFinalizerThread* thread =  new MMTkFinalizerThread(&finalizer_thread_entry);

    // At this point it may be possible that no osthread was created for the
    // JavaThread due to lack of memory. We would have to throw an exception
    // in that case. However, since this must work and we do not allow
    // exceptions anyway, check and abort if this fails.
    if (thread == NULL || thread->osthread() == NULL) {
      vm_exit_during_initialization("java.lang.OutOfMemoryError",
                                    os::native_thread_creation_failed_msg());
    }

    java_lang_Thread::set_thread(thread_oop(), thread);
    java_lang_Thread::set_priority(thread_oop(), NearMaxPriority);
    java_lang_Thread::set_daemon(thread_oop());
    thread->set_threadObj(thread_oop());
    instance = thread;

    Threads::add(thread);
    Thread::start(thread);
  }
}

void MMTkFinalizerThread::finalizer_thread_entry(JavaThread* thread, TRAPS) {
  MMTkFinalizerThread* this_thread = MMTkFinalizerThread::instance;
  while (true) {
    // Wait until scheduled
    {
      ThreadBlockInVM tbivm(thread);
      MutexLockerEx mu(this_thread->m, Mutex::_no_safepoint_check_flag);
      while (!this_thread->is_scheduled) {
        this_thread->m->wait(Mutex::_no_safepoint_check_flag);
      }
      this_thread->is_scheduled = false; // Consume this request so we can accept the next.
    }

    // finalize objects
    while (true) {
      void* obj_ref = get_finalized_object();
      if (obj_ref != NULL) {
        instanceOop obj = (instanceOop) obj_ref;

        // Invoke finalize()
        {
          HandleMark hm;
          JavaValue ret(T_VOID);
          instanceHandle handle_obj(this_thread, obj);
          TempNewSymbol finalize_method = SymbolTable::new_symbol("finalize", this_thread);
          Symbol* sig = vmSymbols::void_method_signature();

          JavaCalls::call_virtual(&ret, handle_obj, obj->klass(), finalize_method, sig, this_thread);
        }
      } else {
        break;
      }
    }
  }
}

MMTkFinalizerThread::MMTkFinalizerThread(ThreadFunction entry_point) : JavaThread(entry_point) {
  this->is_scheduled = false;
  this->m = new Monitor(Mutex::suspend_resume, "mmtk-finalizer-monitor", true, Monitor::_safepoint_check_never);
}

void MMTkFinalizerThread::schedule() {
  assert(!Thread::current()->is_Java_thread(), "Supposed to be called by GC thread. Actually called by JavaThread.");
  MutexLockerEx mu(this->m, Mutex::_no_safepoint_check_flag);
  if (!this->is_scheduled) {
    this->is_scheduled = true;
    this->m->notify();
  }
}
