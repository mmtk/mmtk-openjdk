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
#include "opto/addnode.hpp"
#include "opto/callnode.hpp"
#include "opto/compile.hpp"
#include "opto/node.hpp"
#include "utilities/macros.hpp"
#include "mmtkBarrierSetC2.hpp"
#include "mmtkBarrierSet.hpp"
#include "mmtk.h"
#include "mmtkMutator.hpp"


void MMTkBarrierSetC2::expand_allocate(
            PhaseMacroExpand* x,
            AllocateNode* alloc, // allocation node to be expanded
            Node* length,  // array length for an array allocation
            const TypeFunc* slow_call_type, // Type of slow call
            address slow_call_address  // Address of slow call
    )
{
  Node* ctrl = alloc->in(TypeFunc::Control);
  Node* mem  = alloc->in(TypeFunc::Memory);
  Node* i_o  = alloc->in(TypeFunc::I_O);
  Node* size_in_bytes     = alloc->in(AllocateNode::AllocSize);
  Node* klass_node        = alloc->in(AllocateNode::KlassNode);
  Node* initial_slow_test = alloc->in(AllocateNode::InitialTest);

  assert(ctrl != NULL, "must have control");
  // We need a Region and corresponding Phi's to merge the slow-path and fast-path results.
  // they will not be used if "always_slow" is set
  enum { slow_result_path = 1, fast_result_path = 2 };
  Node *result_region = NULL;
  Node *result_phi_rawmem = NULL;
  Node *result_phi_rawoop = NULL;
  Node *result_phi_i_o = NULL;

  // The initial slow comparison is a size check, the comparison
  // we want to do is a BoolTest::gt
  bool always_slow = false;
  int tv = x->_igvn.find_int_con(initial_slow_test, -1);
  if (tv >= 0) {
    always_slow = (tv == 1);
    initial_slow_test = NULL;
  } else {
    initial_slow_test = BoolNode::make_predicate(initial_slow_test, &x->_igvn);
  }

  if (x->C->env()->dtrace_alloc_probes() || !MMTK_ENABLE_ALLOCATION_FASTPATH) {
    // Force slow-path allocation
    always_slow = true;
    initial_slow_test = NULL;
  }


  enum { too_big_or_final_path = 1, need_gc_path = 2 };
  Node *slow_region = NULL;
  Node *toobig_false = ctrl;

  assert (initial_slow_test == NULL || !always_slow, "arguments must be consistent");
  // printf("c2 alloc tph, always_slow=%s\n", always_slow ? "true" : "false");
  // generate the initial test if necessary
  if (initial_slow_test != NULL ) {
    slow_region = new RegionNode(3);

    // Now make the initial failure test.  Usually a too-big test but
    // might be a TRUE for finalizers or a fancy class check for
    // newInstance0.
    IfNode *toobig_iff = new IfNode(ctrl, initial_slow_test, PROB_MIN, COUNT_UNKNOWN);
    x->transform_later(toobig_iff);
    // Plug the failing-too-big test into the slow-path region
    Node *toobig_true = new IfTrueNode( toobig_iff );
    x->transform_later(toobig_true);
    slow_region    ->init_req( too_big_or_final_path, toobig_true );
    toobig_false = new IfFalseNode( toobig_iff );
    x->transform_later(toobig_false);
  } else {         // No initial test, just fall into next case
    toobig_false = ctrl;
    debug_only(slow_region = NodeSentinel);
  }

  Node *slow_mem = mem;  // save the current memory state for slow path
  // generate the fast allocation code unless we know that the initial test will always go slow
  if (!always_slow) {
    // Fast path modifies only raw memory.
    if (mem->is_MergeMem()) {
      mem = mem->as_MergeMem()->memory_at(Compile::AliasIdxRaw);
    }

    Node* eden_top_adr;
    Node* eden_end_adr;

    {
      // We always use the default allocator.
      // But we need to figure out which allocator we are using by querying MMTk.
      AllocatorSelector selector = get_allocator_mapping(AllocatorDefault);

      // Only bump pointer allocator is implemented.
      if (selector.tag != TAG_BUMP_POINTER) {
        fatal("unimplemented allocator fastpath\n");
      }

      // Calculat offsets of top and end. We now assume we are using bump pointer.
      int allocator_base_offset = in_bytes(JavaThread::third_party_heap_mutator_offset())
        + in_bytes(byte_offset_of(MMTkMutatorContext, allocators))
        + in_bytes(byte_offset_of(Allocators, bump_pointer))
        + selector.index * sizeof(BumpAllocator);

      Node* thread = x->transform_later(new ThreadLocalNode());
      int tlab_top_offset = allocator_base_offset + in_bytes(byte_offset_of(BumpAllocator, cursor));
      int tlab_end_offset = allocator_base_offset + in_bytes(byte_offset_of(BumpAllocator, limit));
      eden_top_adr = x->basic_plus_adr(x->top()/*not oop*/, thread, tlab_top_offset);
      eden_end_adr = x->basic_plus_adr(x->top()/*not oop*/, thread, tlab_end_offset);
    }

    // set_eden_pointers(eden_top_adr, eden_end_adr);

    // Load Eden::end.  Loop invariant and hoisted.
    //
    // Note: We set the control input on "eden_end" and "old_eden_top" when using
    //       a TLAB to work around a bug where these values were being moved across
    //       a safepoint.  These are not oops, so they cannot be include in the oop
    //       map, but they can be changed by a GC.   The proper way to fix this would
    //       be to set the raw memory state when generating a  SafepointNode.  However
    //       this will require extensive changes to the loop optimization in order to
    //       prevent a degradation of the optimization.
    //       See comment in memnode.hpp, around line 227 in class LoadPNode.
    Node *eden_end = x->make_load(ctrl, mem, eden_end_adr, 0, TypeRawPtr::BOTTOM, T_ADDRESS);

    // allocate the Region and Phi nodes for the result
    result_region = new RegionNode(3);
    result_phi_rawmem = new PhiNode(result_region, Type::MEMORY, TypeRawPtr::BOTTOM);
    result_phi_rawoop = new PhiNode(result_region, TypeRawPtr::BOTTOM);
    result_phi_i_o    = new PhiNode(result_region, Type::ABIO); // I/O is used for Prefetch

    // We need a Region for the loop-back contended case.
    enum { fall_in_path = 1, contended_loopback_path = 2 };
    Node *contended_region = toobig_false;
    Node *contended_phi_rawmem = mem;

    // Load(-locked) the heap top.
    // See note above concerning the control input when using a TLAB
    Node *old_eden_top = new LoadPNode(ctrl, contended_phi_rawmem, eden_top_adr, TypeRawPtr::BOTTOM, TypeRawPtr::BOTTOM, MemNode::unordered);

    x->transform_later(old_eden_top);
    // Add to heap top to get a new heap top
    Node *new_eden_top = new AddPNode(x->top(), old_eden_top, size_in_bytes);
    x->transform_later(new_eden_top);
    // Check for needing a GC; compare against heap end
    Node *needgc_cmp = new CmpPNode(new_eden_top, eden_end);
    x->transform_later(needgc_cmp);
    Node *needgc_bol = new BoolNode(needgc_cmp, BoolTest::ge);
    x->transform_later(needgc_bol);
    IfNode *needgc_iff = new IfNode(contended_region, needgc_bol, PROB_UNLIKELY_MAG(4), COUNT_UNKNOWN);
    x->transform_later(needgc_iff);

    // Plug the failing-heap-space-need-gc test into the slow-path region
    Node *needgc_true = new IfTrueNode(needgc_iff);
    x->transform_later(needgc_true);
    if (initial_slow_test) {
      slow_region->init_req(need_gc_path, needgc_true);
      // This completes all paths into the slow merge point
      x->transform_later(slow_region);
    } else {                      // No initial slow path needed!
      // Just fall from the need-GC path straight into the VM call.
      slow_region = needgc_true;
    }
    // No need for a GC.  Setup for the Store-Conditional
    Node *needgc_false = new IfFalseNode(needgc_iff);
    x->transform_later(needgc_false);

    // Grab regular I/O before optional prefetch may change it.
    // Slow-path does no I/O so just set it to the original I/O.
    result_phi_i_o->init_req(slow_result_path, i_o);

    // i_o = prefetch_allocation(i_o, needgc_false, contended_phi_rawmem,
    //                           old_eden_top, new_eden_top, length);

    // Name successful fast-path variables
    Node* fast_oop = old_eden_top;
    Node* fast_oop_ctrl;
    Node* fast_oop_rawmem;

    // Store (-conditional) the modified eden top back down.
    // StorePConditional produces flags for a test PLUS a modified raw
    // memory state.
    Node* store_eden_top =
      new StorePNode(needgc_false, contended_phi_rawmem, eden_top_adr,
                            TypeRawPtr::BOTTOM, new_eden_top, MemNode::unordered);
    x->transform_later(store_eden_top);
    fast_oop_ctrl = needgc_false; // No contention, so this is the fast path
    fast_oop_rawmem = store_eden_top;

    InitializeNode* init = alloc->initialization();
    fast_oop_rawmem = x->initialize_object(alloc,
                                        fast_oop_ctrl, fast_oop_rawmem, fast_oop,
                                        klass_node, length, size_in_bytes);

    // If initialization is performed by an array copy, any required
    // MemBarStoreStore was already added. If the object does not
    // escape no need for a MemBarStoreStore. If the object does not
    // escape in its initializer and memory barrier (MemBarStoreStore or
    // stronger) is already added at exit of initializer, also no need
    // for a MemBarStoreStore. Otherwise we need a MemBarStoreStore
    // so that stores that initialize this object can't be reordered
    // with a subsequent store that makes this object accessible by
    // other threads.
    // Other threads include java threads and JVM internal threads
    // (for example concurrent GC threads). Current concurrent GC
    // implementation: CMS and G1 will not scan newly created object,
    // so it's safe to skip storestore barrier when allocation does
    // not escape.
    if (!alloc->does_not_escape_thread() &&
        !alloc->is_allocation_MemBar_redundant() &&
        (init == NULL || !init->is_complete_with_arraycopy())) {
      if (init == NULL || init->req() < InitializeNode::RawStores) {
        // No InitializeNode or no stores captured by zeroing
        // elimination. Simply add the MemBarStoreStore after object
        // initialization.
        MemBarNode* mb = MemBarNode::make(x->C, Op_MemBarStoreStore, Compile::AliasIdxBot);
        x->transform_later(mb);

        mb->init_req(TypeFunc::Memory, fast_oop_rawmem);
        mb->init_req(TypeFunc::Control, fast_oop_ctrl);
        fast_oop_ctrl = new ProjNode(mb,TypeFunc::Control);
        x->transform_later(fast_oop_ctrl);
        fast_oop_rawmem = new ProjNode(mb,TypeFunc::Memory);
        x->transform_later(fast_oop_rawmem);
      } else {
        // Add the MemBarStoreStore after the InitializeNode so that
        // all stores performing the initialization that were moved
        // before the InitializeNode happen before the storestore
        // barrier.

        Node* init_ctrl = init->proj_out_or_null(TypeFunc::Control);
        Node* init_mem = init->proj_out_or_null(TypeFunc::Memory);

        MemBarNode* mb = MemBarNode::make(x->C, Op_MemBarStoreStore, Compile::AliasIdxBot);
        x->transform_later(mb);

        Node* ctrl = new ProjNode(init,TypeFunc::Control);
        x->transform_later(ctrl);
        Node* mem = new ProjNode(init,TypeFunc::Memory);
        x->transform_later(mem);

        // The MemBarStoreStore depends on control and memory coming
        // from the InitializeNode
        mb->init_req(TypeFunc::Memory, mem);
        mb->init_req(TypeFunc::Control, ctrl);

        ctrl = new ProjNode(mb,TypeFunc::Control);
        x->transform_later(ctrl);
        mem = new ProjNode(mb,TypeFunc::Memory);
        x->transform_later(mem);

        // All nodes that depended on the InitializeNode for control
        // and memory must now depend on the MemBarNode that itself
        // depends on the InitializeNode
        if (init_ctrl != NULL) {
          x->_igvn.replace_node(init_ctrl, ctrl);
        }
        if (init_mem != NULL) {
          x->_igvn.replace_node(init_mem, mem);
        }
      }
    }

    if (x->C->env()->dtrace_extended_probes()) {
      // Slow-path call
      int size = TypeFunc::Parms + 2;
      CallLeafNode *call = new CallLeafNode(OptoRuntime::dtrace_object_alloc_Type(),
                                            CAST_FROM_FN_PTR(address, SharedRuntime::dtrace_object_alloc_base),
                                            "dtrace_object_alloc",
                                            TypeRawPtr::BOTTOM);

      // Get base of thread-local storage area
      Node* thread = new ThreadLocalNode();
      x->transform_later(thread);

      call->init_req(TypeFunc::Parms+0, thread);
      call->init_req(TypeFunc::Parms+1, fast_oop);
      call->init_req(TypeFunc::Control, fast_oop_ctrl);
      call->init_req(TypeFunc::I_O    , x->top()); // does no i/o
      call->init_req(TypeFunc::Memory , fast_oop_rawmem);
      call->init_req(TypeFunc::ReturnAdr, alloc->in(TypeFunc::ReturnAdr));
      call->init_req(TypeFunc::FramePtr, alloc->in(TypeFunc::FramePtr));
      x->transform_later(call);
      fast_oop_ctrl = new ProjNode(call,TypeFunc::Control);
      x->transform_later(fast_oop_ctrl);
      fast_oop_rawmem = new ProjNode(call,TypeFunc::Memory);
      x->transform_later(fast_oop_rawmem);
    }

    // Plug in the successful fast-path into the result merge point
    result_region    ->init_req(fast_result_path, fast_oop_ctrl);
    result_phi_rawoop->init_req(fast_result_path, fast_oop);
    result_phi_i_o   ->init_req(fast_result_path, i_o);
    result_phi_rawmem->init_req(fast_result_path, fast_oop_rawmem);
  } else {
    slow_region = ctrl;
    result_phi_i_o = i_o; // Rename it to use in the following code.
  }

  // Generate slow-path call
  CallNode *call = new CallStaticJavaNode(slow_call_type, slow_call_address,
                               OptoRuntime::stub_name(slow_call_address),
                               alloc->jvms()->bci(),
                               TypePtr::BOTTOM);
  call->init_req( TypeFunc::Control, slow_region );
  call->init_req( TypeFunc::I_O    , x->top() )     ;   // does no i/o
  call->init_req( TypeFunc::Memory , slow_mem ); // may gc ptrs
  call->init_req( TypeFunc::ReturnAdr, alloc->in(TypeFunc::ReturnAdr) );
  call->init_req( TypeFunc::FramePtr, alloc->in(TypeFunc::FramePtr) );

  call->init_req(TypeFunc::Parms+0, klass_node);
  if (length != NULL) {
    call->init_req(TypeFunc::Parms+1, length);
  }

  // Copy debug information and adjust JVMState information, then replace
  // allocate node with the call
  x->copy_call_debug_info((CallNode *) alloc,  call);
  if (!always_slow) {
    call->set_cnt(PROB_UNLIKELY_MAG(4));  // Same effect as RC_UNCOMMON.
  } else {
    // Hook i_o projection to avoid its elimination during allocation
    // replacement (when only a slow call is generated).
    call->set_req(TypeFunc::I_O, result_phi_i_o);
  }
  x->_igvn.replace_node(alloc, call);
  x->transform_later(call);

  // Identify the output projections from the allocate node and
  // adjust any references to them.
  // The control and io projections look like:
  //
  //        v---Proj(ctrl) <-----+   v---CatchProj(ctrl)
  //  Allocate                   Catch
  //        ^---Proj(io) <-------+   ^---CatchProj(io)
  //
  //  We are interested in the CatchProj nodes.
  //
  x->extract_call_projections(call);

  // An allocate node has separate memory projections for the uses on
  // the control and i_o paths. Replace the control memory projection with
  // result_phi_rawmem (unless we are only generating a slow call when
  // both memory projections are combined)
  if (!always_slow && x->_memproj_fallthrough != NULL) {
    for (DUIterator_Fast imax, i = x->_memproj_fallthrough->fast_outs(imax); i < imax; i++) {
      Node *use = x->_memproj_fallthrough->fast_out(i);
      x->_igvn.rehash_node_delayed(use);
      imax -= x->replace_input(use, x->_memproj_fallthrough, result_phi_rawmem);
      // back up iterator
      --i;
    }
  }
  // Now change uses of _memproj_catchall to use _memproj_fallthrough and delete
  // _memproj_catchall so we end up with a call that has only 1 memory projection.
  if (x->_memproj_catchall != NULL ) {
    if (x->_memproj_fallthrough == NULL) {
      x->_memproj_fallthrough = new ProjNode(call, TypeFunc::Memory);
      x->transform_later(x->_memproj_fallthrough);
    }
    for (DUIterator_Fast imax, i = x->_memproj_catchall->fast_outs(imax); i < imax; i++) {
      Node *use = x->_memproj_catchall->fast_out(i);
      x->_igvn.rehash_node_delayed(use);
      imax -= x->replace_input(use, x->_memproj_catchall, x->_memproj_fallthrough);
      // back up iterator
      --i;
    }
    assert(x->_memproj_catchall->outcnt() == 0, "all uses must be deleted");
    x->_igvn.remove_dead_node(x->_memproj_catchall);
  }

  // An allocate node has separate i_o projections for the uses on the control
  // and i_o paths. Always replace the control i_o projection with result i_o
  // otherwise incoming i_o become dead when only a slow call is generated
  // (it is different from memory projections where both projections are
  // combined in such case).
  if (x->_ioproj_fallthrough != NULL) {
    for (DUIterator_Fast imax, i = x->_ioproj_fallthrough->fast_outs(imax); i < imax; i++) {
      Node *use = x->_ioproj_fallthrough->fast_out(i);
      x->_igvn.rehash_node_delayed(use);
      imax -= x->replace_input(use, x->_ioproj_fallthrough, result_phi_i_o);
      // back up iterator
      --i;
    }
  }
  // Now change uses of _ioproj_catchall to use _ioproj_fallthrough and delete
  // _ioproj_catchall so we end up with a call that has only 1 i_o projection.
  if (x->_ioproj_catchall != NULL ) {
    if (x->_ioproj_fallthrough == NULL) {
      x->_ioproj_fallthrough = new ProjNode(call, TypeFunc::I_O);
      x->transform_later(x->_ioproj_fallthrough);
    }
    for (DUIterator_Fast imax, i = x->_ioproj_catchall->fast_outs(imax); i < imax; i++) {
      Node *use = x->_ioproj_catchall->fast_out(i);
      x->_igvn.rehash_node_delayed(use);
      imax -= x->replace_input(use, x->_ioproj_catchall, x->_ioproj_fallthrough);
      // back up iterator
      --i;
    }
    assert(x->_ioproj_catchall->outcnt() == 0, "all uses must be deleted");
    x->_igvn.remove_dead_node(x->_ioproj_catchall);
  }

  // if we generated only a slow call, we are done
  if (always_slow) {
    // Now we can unhook i_o.
    if (result_phi_i_o->outcnt() > 1) {
      call->set_req(TypeFunc::I_O, x->top());
    } else {
      assert(result_phi_i_o->unique_ctrl_out() == call, "");
      // Case of new array with negative size known during compilation.
      // AllocateArrayNode::Ideal() optimization disconnect unreachable
      // following code since call to runtime will throw exception.
      // As result there will be no users of i_o after the call.
      // Leave i_o attached to this call to avoid problems in preceding graph.
    }
    return;
  }


  if (x->_fallthroughcatchproj != NULL) {
    ctrl = x->_fallthroughcatchproj->clone();
    x->transform_later(ctrl);
    x->_igvn.replace_node(x->_fallthroughcatchproj, result_region);
  } else {
    ctrl = x->top();
  }
  Node *slow_result;
  if (x->_resproj == NULL) {
    // no uses of the allocation result
    slow_result = x->top();
  } else {
    slow_result = x->_resproj->clone();
    x->transform_later(slow_result);
    x->_igvn.replace_node(x->_resproj, result_phi_rawoop);
  }

  // Plug slow-path into result merge point
  result_region    ->init_req( slow_result_path, ctrl );
  result_phi_rawoop->init_req( slow_result_path, slow_result);
  result_phi_rawmem->init_req( slow_result_path, x->_memproj_fallthrough );
  x->transform_later(result_region);
  x->transform_later(result_phi_rawoop);
  x->transform_later(result_phi_rawmem);
  x->transform_later(result_phi_i_o);
  // This completes all paths into the result merge point
}


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

  if (!MMTkBarrierSet::enable_write_barrier || !access.is_oop()) {
    return BarrierSetC2::store_at_resolved(access, val);
  }

  Node* store = BarrierSetC2::store_at_resolved(access, val);

  record_modified_node(kit, access.base());

  return store;
}

Node* MMTkBarrierSetC2::atomic_cmpxchg_val_at_resolved(C2AtomicAccess& access, Node* expected_val,
                                                         Node* new_val, const Type* value_type) const {
  DecoratorSet decorators = access.decorators();
  GraphKit* kit = access.kit();
  bool in_heap = (decorators & IN_HEAP) != 0;

  if (!MMTkBarrierSet::enable_write_barrier || !access.is_oop()) {
    return BarrierSetC2::atomic_cmpxchg_val_at_resolved(access, expected_val, new_val, value_type);
  }

  Node* result = BarrierSetC2::atomic_cmpxchg_val_at_resolved(access, expected_val, new_val, value_type);

  record_modified_node(kit, access.base());

  return result;
}

Node* MMTkBarrierSetC2::atomic_cmpxchg_bool_at_resolved(C2AtomicAccess& access, Node* expected_val,
                                                          Node* new_val, const Type* value_type) const {
  DecoratorSet decorators = access.decorators();
  GraphKit* kit = access.kit();
  bool in_heap = (decorators & IN_HEAP) != 0;

  if (!MMTkBarrierSet::enable_write_barrier || !access.is_oop()) {
    return BarrierSetC2::atomic_cmpxchg_bool_at_resolved(access, expected_val, new_val, value_type);
  }

  Node* load_store = BarrierSetC2::atomic_cmpxchg_bool_at_resolved(access, expected_val, new_val, value_type);

  record_modified_node(kit, access.base());

  return load_store;
}

Node* MMTkBarrierSetC2::atomic_xchg_at_resolved(C2AtomicAccess& access, Node* new_val, const Type* value_type) const {
  DecoratorSet decorators = access.decorators();
  GraphKit* kit = access.kit();

  Node* result = BarrierSetC2::atomic_xchg_at_resolved(access, new_val, value_type);

  bool in_heap = (decorators & IN_HEAP) != 0;

  if (!MMTkBarrierSet::enable_write_barrier || !access.is_oop()) {
    return result;
  }

  record_modified_node(kit, access.base());

  return result;
}

void MMTkBarrierSetC2::clone(GraphKit* kit, Node* src, Node* dst, Node* size, bool is_array) const {
  BarrierSetC2::clone(kit, src, dst, size, is_array);
  if (MMTkBarrierSet::enable_write_barrier) record_modified_node(kit, dst);
}

const TypeFunc* record_modified_node_entry_Type() {
  const Type **fields = TypeTuple::fields(1);
  fields[TypeFunc::Parms+0] = TypeOopPtr::BOTTOM; // oop src
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+1, fields);
  fields = TypeTuple::fields(0);
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+0, fields);
  return TypeFunc::make(domain, range);
}

const TypeFunc* record_modified_edge_entry_Type() {
  const Type **fields = TypeTuple::fields(1);
  fields[TypeFunc::Parms+0] = TypeOopPtr::BOTTOM; // oop src
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+1, fields);
  fields = TypeTuple::fields(0);
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+0, fields);
  return TypeFunc::make(domain, range);
}

void MMTkBarrierSetC2::record_modified_edge(GraphKit* kit, Node* slot) const {
  IdealKit ideal(kit, true);
  const TypeFunc *tf = record_modified_edge_entry_Type();
  Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkBarrierRuntime::record_modified_edge), "record_modified_edge", slot);
  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

void MMTkBarrierSetC2::record_modified_node(GraphKit* kit, Node* node) const {
  IdealKit ideal(kit, true);
  const TypeFunc *tf = record_modified_node_entry_Type();
  Node* x = __ make_leaf_call(tf, CAST_FROM_FN_PTR(address, MMTkBarrierRuntime::record_modified_node), "record_modified_node", node);
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

  return strcmp(call->_name, "record_modified_edge") == 0 || strcmp(call->_name, "record_modified_node") == 0;
}

