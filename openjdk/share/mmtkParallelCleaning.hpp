/*
 * Copyright (c) 2015, 2020, Oracle and/or its affiliates. All rights reserved.
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

#ifndef MMTK_OPENJDK_MMTK_PARALLELCLEANING_HPP
#define MMTK_OPENJDK_MMTK_PARALLELCLEANING_HPP

#include "gc/shared/oopStorageParState.hpp"
#include "gc/shared/workerThread.hpp"
#include "gc/shared/parallelCleaning.hpp"

namespace mmtk {

class MMTkCodeCacheUnloadingTask {
  const bool                _unloading_occurred;
  const uint                _num_workers;

  // Variables used to claim nmethods.
  CompiledMethod* _first_nmethod;
  CompiledMethod* volatile _claimed_nmethod;

public:
  MMTkCodeCacheUnloadingTask(uint num_workers, bool unloading_occurred);

  ~MMTkCodeCacheUnloadingTask();

private:
  static const int MaxClaimNmethods = 16;
  void claim_nmethods(CompiledMethod** claimed_nmethods, int *num_claimed_nmethods);

public:
  // Cleaning and unloading of nmethods.
  void work(uint worker_id);
};

// To minimize the remark pause times, the tasks below are done in parallel.
class ParallelCleaningTask : public WorkerTask {
private:
  bool                            _unloading_occurred;
  MMTkCodeCacheUnloadingTask      _code_cache_task;
  KlassCleaningTask               _klass_cleaning_task;

public:
  // The constructor is run in the VMThread.
  ParallelCleaningTask(uint num_workers, bool unloading_occurred);

  // The parallel work done by all worker threads.
  void work(uint worker_id);
};

}

#endif // MMTK_OPENJDK_MMTK_PARALLELCLEANING_HPP
