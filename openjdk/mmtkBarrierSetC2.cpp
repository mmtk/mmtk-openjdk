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

#include "precompiled.hpp"
#include "opto/arraycopynode.hpp"
#include "opto/graphKit.hpp"
#include "opto/idealKit.hpp"
#include "opto/narrowptrnode.hpp"
#include "opto/macro.hpp"
#include "opto/type.hpp"
#include "utilities/macros.hpp"
#include "mmtkBarrierSetC2.hpp"
#include "mmtkBarrierSet.hpp"


#define __ ideal.

Node* MMTkBarrierSetC2::store_at_resolved(C2Access& access, C2AccessValue& val) const {
  DecoratorSet decorators = access.decorators();
  GraphKit* kit = access.kit();

  const TypePtr* adr_type = access.addr().type();
  Node* adr = access.addr().node();

  bool is_array = (decorators & IS_ARRAY) != 0;
  bool anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool use_precise = is_array || anonymous;

  if (!MMTK_ENABLE_WRITE_BARRIER || !access.is_oop() || (!in_heap && !anonymous)) {
    return BarrierSetC2::store_at_resolved(access, val);
  }

  uint adr_idx = kit->C->get_alias_index(adr_type);
  assert(adr_idx != Compile::AliasIdxTop, "use other store_to_memory factory" );
  
  Node* store = BarrierSetC2::store_at_resolved(access, val);
  write_barrier(kit, true /* do_load */, kit->control(), access.base(), adr, adr_idx, val.node(),
              static_cast<const TypeOopPtr*>(val.type()), NULL /* pre_val */, access.type());
  return store;
}


const TypeFunc* write_barrier_slow_entry_Type() {
  const Type **fields = TypeTuple::fields(3);
  // fields[TypeFunc::Parms+0] = TypeRawPtr::NOTNULL; // JavaThread* thread
  fields[TypeFunc::Parms+0] = TypeOopPtr::NOTNULL; // oop src
  fields[TypeFunc::Parms+1] = TypeOopPtr::BOTTOM; // oop new_val
  fields[TypeFunc::Parms+2] = TypeOopPtr::BOTTOM; // oop new_val
  // fields[TypeFunc::Parms+2] = TypeOopPtr::BOTTOM; // oop* slot
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+3, fields);

  // create result type (range)
  fields = TypeTuple::fields(0);
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+0, fields);

  return TypeFunc::make(domain, range);
}

void MMTkBarrierSetC2::write_barrier(GraphKit* kit,
                                 bool do_load,
                                 Node* ctl,
                                 Node* obj,
                                 Node* adr,
                                 uint alias_idx,
                                 Node* val,
                                 const TypeOopPtr* val_type,
                                 Node* pre_val,
                                 BasicType bt) const {
  IdealKit ideal(kit, true);

  // Node* tls = __ thread(); // ThreadLocalStorage

  const TypeFunc *tf = write_barrier_slow_entry_Type();
  Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkBarrierRuntime::write_barrier_slow), "write_barrier_slow", obj, adr, val);
  
  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

bool MMTkBarrierSetC2::is_gc_barrier_node(Node* node) const {
  if (node->Opcode() != Op_CallLeaf) {
    return false;
  }
  CallLeafNode *call = node->as_CallLeaf();
  if (call->_name == NULL) {
    return false;
  }

  return strcmp(call->_name, "write_barrier_slow") == 0;// || strcmp(call->_name, "write_ref_field_post_entry") == 0;
}