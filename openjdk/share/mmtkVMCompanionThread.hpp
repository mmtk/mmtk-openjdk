/*
 * Copyright (c) 1998, 2016, Oracle and/or its affiliates. All rights reserved.
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

#ifndef MMTK_OPENJDK_MMTK_VM_COMPANION_THREAD_HPP
#define MMTK_OPENJDK_MMTK_VM_COMPANION_THREAD_HPP

#include "mmtkVMOperation.hpp"
#include "runtime/mutex.hpp"
#include "runtime/perfData.hpp"
#include "runtime/thread.hpp"
#include "runtime/vmOperations.hpp"

// This thread cooperates with the VMThread to allow stopping the world without
// blocking any GC threads.
//
// In HotSpot, the way to stop all Java threads for stop-the-world GC is
// letting the VMThread execute a blocking VM_Operation.  However, the MMTk
// expects the VM to provide two non-blocking methods to stop and start the
// the world, whthout blocking the callers.  This thread bridges the API gap
// by calling VMThread::execute on behalf of GC threads upon reques so that it
// blocks this thread instead of GC threads.
class MMTkVMCompanionThread: public NamedThread {
public:
  enum stw_state {
    _threads_suspended,
    _threads_resumed,
  };
private:
  Monitor* _lock;
  stw_state _desired_state;
  stw_state _reached_state;

public:
  // Constructor
  MMTkVMCompanionThread();
  ~MMTkVMCompanionThread();

  virtual void run() override;

  // Interface for MMTk Core
  void request(stw_state desired_state, bool wait_until_reached);
  void wait_for_reached(stw_state reached_state);

  // Interface for the VM_MMTkSTWOperation
  void reach_suspended_and_wait_for_resume();
};

#endif // MMTK_OPENJDK_MMTK_VM_COMPANION_THREAD_HPP
