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

#ifndef MMTK_OPENJDK_MMTK_COLLECTOR_THREAD_HPP
#define MMTK_OPENJDK_MMTK_COLLECTOR_THREAD_HPP

#include "runtime/perfData.hpp"
#include "runtime/nonJavaThread.hpp"

class MMTkCollectorThread: public NamedThread {
  void* _context;
public:
  // Constructor
  MMTkCollectorThread(void* _context);

  // No destruction allowed
  ~MMTkCollectorThread() {
    guarantee(false, "MMTkCollectorThread deletion must fix the race with VM termination");
  }

  inline void* get_context() {
    return third_party_heap_collector;
  }

  // Entry for starting vm thread
  virtual void run();
};

#endif // MMTK_OPENJDK_MMTK_COLLECTOR_THREAD_HPP
