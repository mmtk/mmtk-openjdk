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

#ifndef MMTK_OPENJDK_MMTK_BARRIER_SET_C2_HPP
#define MMTK_OPENJDK_MMTK_BARRIER_SET_C2_HPP

#include "gc/shared/c2/barrierSetC2.hpp"
#include "opto/addnode.hpp"
#include "opto/arraycopynode.hpp"
#include "opto/callnode.hpp"
#include "opto/compile.hpp"
#include "opto/convertnode.hpp"
#include "opto/graphKit.hpp"
#include "opto/idealKit.hpp"
#include "opto/macro.hpp"
#include "opto/narrowptrnode.hpp"
#include "opto/node.hpp"
#include "opto/type.hpp"

class TypeOopPtr;
class PhaseMacroExpand;
class AllocateNode;
class Node;
class TypeFunc;

class MMTkBarrierSetC2: public BarrierSetC2 {
protected:
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

public:
  virtual void clone(GraphKit* kit, Node* src, Node* dst, Node* size, bool is_array) const {
    BarrierSetC2::clone(kit, src, dst, size, is_array);
  }
  virtual bool array_copy_requires_gc_barriers(BasicType type) const {
    return true;
  }
  virtual bool is_gc_barrier_node(Node* node) const {
    return BarrierSetC2::is_gc_barrier_node(node);
  }
  static void expand_allocate(PhaseMacroExpand* x,
                              AllocateNode* alloc, // allocation node to be expanded
                              Node* length,  // array length for an array allocation
                              const TypeFunc* slow_call_type, // Type of slow call
                              address slow_call_address);  // Address of slow call
};

class MMTkIdealKit: public IdealKit {
  inline void build_type_func_helper(const Type** fields) {}

  template<class T, class... Types>
  inline void build_type_func_helper(const Type** fields, T t, Types... ts) {
    fields[0] = t;
    build_type_func_helper(fields + 1, ts...);
  }
public:
  using IdealKit::IdealKit;
  inline Node* LShiftX(Node* l, Node* r) { return transform(new LShiftXNode(l, r)); }
  inline Node* AndX(Node* l, Node* r) { return transform(new AndXNode(l, r)); }
  inline Node* ConvL2I(Node* x) { return transform(new ConvL2INode(x)); }
  inline Node* CastXP(Node* x) { return transform(new CastX2PNode(x)); }
  inline Node* URShiftI(Node* l, Node* r) { return transform(new URShiftINode(l, r)); }
  inline Node* ConP(intptr_t ptr) { return makecon(TypeRawPtr::make((address) ptr)); }

  template<class... Types>
  inline const TypeFunc* func_type(Types... types) {
    const int num_types = sizeof...(types);
    const Type** fields = TypeTuple::fields(num_types);
    build_type_func_helper(fields + TypeFunc::Parms, types...);
    const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+num_types, fields);
    fields = TypeTuple::fields(0);
    const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+0, fields);
    return TypeFunc::make(domain, range);
  }
};

#endif // MMTK_OPENJDK_MMTK_BARRIER_SET_C2_HPP
