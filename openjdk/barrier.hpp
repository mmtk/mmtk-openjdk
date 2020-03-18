/*
 * Copyright (c) 2000, 2017, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_GC_MMTK_NOBARRIER_HPP
#define SHARE_VM_GC_MMTK_NOBARRIER_HPP

#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetConfig.hpp"
#include "memory/memRegion.hpp"
#include "oops/access.hpp"
#include "oops/accessBackend.hpp"
#include "oops/oopsHierarchy.hpp"
#include "utilities/fakeRttiSupport.hpp"

// This class provides the interface between a barrier implementation and
// the rest of the system.

class NoBarrier : public BarrierSet {
  friend class VMStructs;
  private:
      MemRegion _whole_heap;

protected:
  virtual void write_ref_array_work(MemRegion mr) ;

protected:
  virtual void write_region_work(MemRegion mr) ;

public:

    NoBarrier(MemRegion whole_heap): BarrierSet((BarrierSet::FakeRtti(BarrierSet::NoBarrier))
        .add_tag(BarrierSet::NoBarrier)),
            _whole_heap(whole_heap){
        
    }
    
  // Inform the BarrierSet that the the covered heap region that starts
  // with "base" has been changed to have the given size (possibly from 0,
  // for initialization.)
  virtual void resize_covered_region(MemRegion new_region) ;

  // If the barrier set imposes any alignment restrictions on boundaries
  // within the heap, this function tells whether they are met.
  virtual bool is_aligned(HeapWord* addr) ;

  // Print a description of the memory for the barrier set
  virtual void print_on(outputStream* st) const ;


  // The AccessBarrier of a BarrierSet subclass is called by the Access API
  // (cf. oops/access.hpp) to perform decorated accesses. GC implementations
  // may override these default access operations by declaring an
  // AccessBarrier class in its BarrierSet. Its accessors will then be
  // automatically resolved at runtime.
  //
  // In order to register a new FooBarrierSet::AccessBarrier with the Access API,
  // the following steps should be taken:
  // 1) Provide an enum "name" for the BarrierSet in barrierSetConfig.hpp
  // 2) Make sure the barrier set headers are included from barrierSetConfig.inline.hpp
  // 3) Provide specializations for BarrierSet::GetName and BarrierSet::GetType.
 template <DecoratorSet decorators, typename BarrierSetT = NoBarrier>
  class AccessBarrier: public BarrierSet::AccessBarrier<decorators, BarrierSetT> {
 
 };
};


template<>
struct BarrierSet::GetName<NoBarrier> {
  static const BarrierSet::Name value = BarrierSet::NoBarrier;
};

template<>
struct BarrierSet::GetType<BarrierSet::NoBarrier> {
  typedef ::NoBarrier type;
};


#endif // SHARE_VM_GC_MMTK_NOBARRIER_HPP