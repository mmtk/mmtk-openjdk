/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
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

#ifndef MMTK_BARRIERSETC2_HPP
#define MMTK_BARRIERSETC2_HPP

#include "gc/shared/c2/barrierSetC2.hpp"

class TypeOopPtr;
class PhaseMacroExpand;
class AllocateNode;
class Node;
class TypeFunc;


class MMTkBarrierC2: public BarrierSetC2 {
public:
  virtual Node* store_at_resolved(C2Access& access, C2AccessValue& val) const {
    return BarrierSetC2::store_at_resolved(access, val);
  }
  virtual Node* atomic_cmpxchg_val_at_resolved(C2AtomicAccess& access, Node* expected_val, Node* new_val, const Type* value_type) const {
    return BarrierSetC2::atomic_cmpxchg_val_at_resolved(access, expected_val, new_val, value_type);
  }
  virtual Node* atomic_cmpxchg_bool_at_resolved(C2AtomicAccess& access, Node* expected_val, Node* new_val, const Type* value_type) const {
    return BarrierSetC2::atomic_cmpxchg_bool_at_resolved(access, expected_val, new_val, value_type);
  }
  virtual Node* atomic_xchg_at_resolved(C2AtomicAccess& access, Node* new_val, const Type* value_type) const {
    return BarrierSetC2::atomic_xchg_at_resolved(access, new_val, value_type);
  }
  virtual void clone(GraphKit* kit, Node* src, Node* dst, Node* size, bool is_array) const {
    BarrierSetC2::clone(kit, src, dst, size, is_array);
  }
  virtual bool is_gc_barrier_node(Node* node) const {
    return BarrierSetC2::is_gc_barrier_node(node);
  }
};

class MMTkBarrierSetC2: public BarrierSetC2 {
protected:
  virtual Node* store_at_resolved(C2Access& access, C2AccessValue& val) const;

  virtual Node* atomic_cmpxchg_val_at_resolved(C2AtomicAccess& access, Node* expected_val,
                                               Node* new_val, const Type* value_type) const;
  virtual Node* atomic_cmpxchg_bool_at_resolved(C2AtomicAccess& access, Node* expected_val,
                                                Node* new_val, const Type* value_type) const;
  virtual Node* atomic_xchg_at_resolved(C2AtomicAccess& access, Node* new_val, const Type* value_type) const;

public:
  virtual void clone(GraphKit* kit, Node* src, Node* dst, Node* size, bool is_array) const;
  virtual bool array_copy_requires_gc_barriers(BasicType type) const { return true; }

  virtual bool is_gc_barrier_node(Node* node) const;
  static void expand_allocate(
            PhaseMacroExpand* x,
            AllocateNode* alloc, // allocation node to be expanded
            Node* length,  // array length for an array allocation
            const TypeFunc* slow_call_type, // Type of slow call
            address slow_call_address  // Address of slow call
    );
};

#endif // MMTK_BARRIERSETC2_HPP
