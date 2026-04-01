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

#include "precompiled.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/stringTable.hpp"
#include "code/codeCache.hpp"
#include "memory/resourceArea.hpp"
#include "prims/resolvedMethodTable.hpp"
#include "logging/log.hpp"
#include "gc/shared/gcCause.hpp"
#include "gc/shared/gcTraceTime.hpp"
#include "gc/shared/gcTraceTime.inline.hpp"
#include "mmtkParallelCleaning.hpp"
#include "mmtk.h"
#include "runtime/atomic.hpp"
#include "oops/access.inline.hpp"
#include "oops/compressedOops.inline.hpp"
#include "oops/oop.inline.hpp"
#include "code/nmethod.hpp"

using namespace JavaClassFile;

namespace mmtk {
MMTkCodeCacheUnloadingTask::MMTkCodeCacheUnloadingTask(uint num_workers, bool unloading_occurred) :
  _unloading_occurred(unloading_occurred),
  _num_workers(num_workers),
  _first_nmethod(nullptr),
  _claimed_nmethod(nullptr) {
  // Get first alive nmethod
  CompiledMethodIterator iter(CompiledMethodIterator::all_blobs);
  if(iter.next()) {
    _first_nmethod = iter.method();
  }
  _claimed_nmethod = _first_nmethod;
}

MMTkCodeCacheUnloadingTask::~MMTkCodeCacheUnloadingTask() {
  CodeCache::verify_icholder_relocations();
}

void MMTkCodeCacheUnloadingTask::claim_nmethods(CompiledMethod** claimed_nmethods, int *num_claimed_nmethods) {
  CompiledMethod* first;
  CompiledMethodIterator last(CompiledMethodIterator::all_blobs);

  do {
    *num_claimed_nmethods = 0;

    first = _claimed_nmethod;
    last = CompiledMethodIterator(CompiledMethodIterator::all_blobs, first);

    if (first != nullptr) {

      for (int i = 0; i < MaxClaimNmethods; i++) {
        if (!last.next()) {
          break;
        }
        claimed_nmethods[i] = last.method();
        (*num_claimed_nmethods)++;
      }
    }

  } while (Atomic::cmpxchg(&_claimed_nmethod, first, last.method()) != first);
}

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
  inline virtual void do_oop(oop* slot) override {
    const auto o = *slot;
    if (o == NULL) return;
    const auto status = read_forwarding_word(o);
    if (is_forwarded(status)) {
      *slot = extract_forwarding_pointer(status);
    }
  }
  inline virtual void do_oop(narrowOop* slot) override {
    narrowOop heap_oop = RawAccess<>::oop_load(slot);
    if (CompressedOops::is_null(heap_oop)) return;
    oop o = CompressedOops::decode_not_null(heap_oop);
    const auto status = read_forwarding_word(o);
    if (is_forwarded(status)) {
      RawAccess<>::oop_store(slot, CompressedOops::encode_not_null(extract_forwarding_pointer(status)));
    }
  }
};

void MMTkCodeCacheUnloadingTask::work(uint worker_id) {
  MMTkForwardClosure cl;
  // The first nmethods is claimed by the first worker.
  if (worker_id == 0 && _first_nmethod != nullptr) {
    _first_nmethod->do_unloading(_unloading_occurred);
    _first_nmethod = nullptr;
  }

  int num_claimed_nmethods;
  CompiledMethod* claimed_nmethods[MaxClaimNmethods];

  while (true) {
    claim_nmethods(claimed_nmethods, &num_claimed_nmethods);

    if (num_claimed_nmethods == 0) {
      break;
    }

    for (int i = 0; i < num_claimed_nmethods; i++) {
      if (claimed_nmethods[i]->is_nmethod()) {
        auto nm = (nmethod*) claimed_nmethods[i];
        nm->oops_do(&cl);
        nm->fix_oop_relocations();
      }
    }
    for (int i = 0; i < num_claimed_nmethods; i++) {
      claimed_nmethods[i]->do_unloading(_unloading_occurred);
    }
  }
}


ParallelCleaningTask::ParallelCleaningTask(uint num_workers, bool unloading_occurred) :
  WorkerTask("Parallel Cleaning"),
  _unloading_occurred(unloading_occurred),
  _code_cache_task(num_workers, unloading_occurred),
  _klass_cleaning_task() {
}

// The parallel work done by all worker threads.
void ParallelCleaningTask::work(uint worker_id) {
  _code_cache_task.work(worker_id);
  // Clean all klasses that were not unloaded.
  // The weak metadata in klass doesn't need to be
  // processed if there was no unloading.
  if (_unloading_occurred) {
    _klass_cleaning_task.work();
  }
}

}