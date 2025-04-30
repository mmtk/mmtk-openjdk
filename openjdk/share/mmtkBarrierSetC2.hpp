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

#ifdef COMPILER2
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
  /// Barrier elision test
  virtual bool can_remove_barrier(GraphKit* kit, PhaseTransform* phase, Node* src, Node* slot, Node* val, bool skip_const_null) const;
  /// Full pre-barrier
  virtual void object_reference_write_pre(GraphKit* kit, Node* src, Node* slot, Node* val) const {}
  /// Full post-barrier
  virtual void object_reference_write_post(GraphKit* kit, Node* src, Node* slot, Node* val) const {}

  virtual Node* store_at_resolved(C2Access& access, C2AccessValue& val) const {
    if (access.is_oop() && access.is_parse_access()) {
      C2ParseAccess& parse_access = static_cast<C2ParseAccess&>(access);
      object_reference_write_pre(parse_access.kit(), access.base(), access.addr().node(), val.node());
    }
    Node* store = BarrierSetC2::store_at_resolved(access, val);
    if (access.is_oop() && access.is_parse_access()) {
      C2ParseAccess& parse_access = static_cast<C2ParseAccess&>(access);
      object_reference_write_post(parse_access.kit(), access.base(), access.addr().node(), val.node());
    }
    return store;
  }
  virtual Node* atomic_cmpxchg_val_at_resolved(C2AtomicParseAccess& access, Node* expected_val, Node* new_val, const Type* value_type) const {
    if (access.is_oop()) object_reference_write_pre(access.kit(), access.base(), access.addr().node(), new_val);
    Node* result = BarrierSetC2::atomic_cmpxchg_val_at_resolved(access, expected_val, new_val, value_type);
    if (access.is_oop()) object_reference_write_post(access.kit(), access.base(), access.addr().node(), new_val);
    return result;
  }
  virtual Node* atomic_cmpxchg_bool_at_resolved(C2AtomicParseAccess& access, Node* expected_val, Node* new_val, const Type* value_type) const {
    if (access.is_oop()) object_reference_write_pre(access.kit(), access.base(), access.addr().node(), new_val);
    Node* load_store = BarrierSetC2::atomic_cmpxchg_bool_at_resolved(access, expected_val, new_val, value_type);
    if (access.is_oop()) object_reference_write_post(access.kit(), access.base(), access.addr().node(), new_val);
    return load_store;
  }
  virtual Node* atomic_xchg_at_resolved(C2AtomicParseAccess& access, Node* new_val, const Type* value_type) const {
    if (access.is_oop()) object_reference_write_pre(access.kit(), access.base(), access.addr().node(), new_val);
    Node* result = BarrierSetC2::atomic_xchg_at_resolved(access, new_val, value_type);
    if (access.is_oop()) object_reference_write_post(access.kit(), access.base(), access.addr().node(), new_val);
    return result;
  }

public:
  virtual void clone(GraphKit* kit, Node* src, Node* dst, Node* size, bool is_array) const {
    BarrierSetC2::clone(kit, src, dst, size, is_array);
  }
  virtual bool array_copy_requires_gc_barriers(bool tightly_coupled_alloc, BasicType type, bool is_clone, bool is_clone_instance, ArrayCopyPhase phase) const {
    return true;
  }
  virtual bool is_gc_barrier_node(Node* node) const {
    if (node->Opcode() != Op_CallLeaf) return false;
    CallLeafNode *call = node->as_CallLeaf();
    return call->_name != NULL && strcmp(call->_name, "mmtk_barrier_call") == 0;
  }
  static void expand_allocate(PhaseMacroExpand* x,
                              AllocateNode* alloc, // allocation node to be expanded
                              Node* length,  // array length for an array allocation
                              const TypeFunc* slow_call_type, // Type of slow call
                              address slow_call_address,  // Address of slow call
                              Node* valid_length_test); // whether length is valid or not
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
#else
class MMTkBarrierSetC2;
#endif

#endif // MMTK_OPENJDK_MMTK_BARRIER_SET_C2_HPP
