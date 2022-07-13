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
#include "mmtk.h"
#include "mmtkBarrierSet.hpp"
#include "mmtkBarrierSetC2.hpp"
#include "mmtkMutator.hpp"
#include "opto/addnode.hpp"
#include "opto/arraycopynode.hpp"
#include "opto/callnode.hpp"
#include "opto/compile.hpp"
#include "opto/graphKit.hpp"
#include "opto/idealKit.hpp"
#include "opto/macro.hpp"
#include "opto/movenode.hpp"
#include "opto/narrowptrnode.hpp"
#include "opto/node.hpp"
#include "opto/runtime.hpp"
#include "opto/type.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/macros.hpp"

void MMTkBarrierSetC2::expand_allocate(PhaseMacroExpand* x,
                                       AllocateNode* alloc, // allocation node to be expanded
                                       Node* length,  // array length for an array allocation
                                       const TypeFunc* slow_call_type, // Type of slow call
                                       address slow_call_address) {  // Address of slow call
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
  // Check if we need initial_slow_test
  int tv = x->_igvn.find_int_con(initial_slow_test, -1);
  if (tv >= 0) {
    always_slow = (tv == 1);
    initial_slow_test = NULL;
  }

  // We will do MMTk size check.
  // The implementation tries not to change the current control flow, and instead evaluate allocation size
  // with max non-los bytes and combine the result with initial_slow_test. If the size check fails, initial_slow_test
  // should be evaluated to true, and we jump to the slowpath.

  // The max non-los bytes from MMTk
  assert(MMTkMutatorContext::max_non_los_default_alloc_bytes != 0, "max_non_los_default_alloc_bytes hasn't been initialized");
  size_t max_non_los_bytes = MMTkMutatorContext::max_non_los_default_alloc_bytes;
  size_t extra_header = 0;
  // We always use the default allocator.
  // But we need to figure out which allocator we are using by querying MMTk.
  AllocatorSelector selector = get_allocator_mapping(AllocatorDefault);
  if (selector.tag == TAG_MARK_COMPACT) extra_header = MMTK_MARK_COMPACT_HEADER_RESERVED_IN_BYTES;

  // Check if allocation size is constant
  long const_size = x->_igvn.find_long_con(size_in_bytes, -1);
  if (const_size >= 0) {
    // Constant alloc size. We know it is non-negative, it is safe to cast to unsigned long and compare with size_t
    if (((unsigned long)const_size) > max_non_los_bytes - extra_header) {
      // We know at JIT time that we need to go to slowpath
      always_slow = true;
      initial_slow_test = NULL;
    }
  } else {
    // Variable alloc size

    // Create a node for the constant and compare with size_in_bytes
    Node *max_non_los_bytes_node = ConLNode::make((long)max_non_los_bytes - extra_header);
    x->transform_later(max_non_los_bytes_node);
    Node *mmtk_size_cmp = new CmpLNode(size_in_bytes, max_non_los_bytes_node);
    x->transform_later(mmtk_size_cmp);
    // Size Check: if size_in_bytes >= max_non_los_bytes
    Node *mmtk_size_bool = new BoolNode(mmtk_size_cmp, BoolTest::ge);
    x->transform_later(mmtk_size_bool);

    if (initial_slow_test == NULL) {
      // If there is no previous initial_slow_test, we use the size check as initial_slow_test
      initial_slow_test = BoolNode::make_predicate(mmtk_size_bool, &x->_igvn);
    } else {
      // If there is existing initial_slow_test, we combine the result by 'or' them together.

      // Conditionally move a value 1 or 0 depends on the size check.
      // This is definitely not optimal. But to make things simple and to avoid changing the original
      // control flow, it is much easier to implement with a cmov. And it should not affect performance much,
      // as var size allocation is rare.
      Node *mmtk_size_cmov = new CMoveINode(mmtk_size_bool, x->intcon(0), x->intcon(1), TypeInt::INT);
      x->transform_later(mmtk_size_cmov);

      // Logical or the size check result (1 or 0) with initial_slow_test
      Node* new_slow_test = new OrINode(mmtk_size_cmov, initial_slow_test);
      x->transform_later(new_slow_test);

      // Use the or'd result as initial_slow_test
      initial_slow_test = BoolNode::make_predicate(new_slow_test, &x->_igvn);
    }
  }

  if (x->C->env()->dtrace_alloc_probes() || !MMTK_ENABLE_ALLOCATION_FASTPATH
      // Malloc allocator has no fastpath
      || (selector.tag == TAG_MALLOC || selector.tag == TAG_LARGE_OBJECT)) {
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
      // Calculate offsets of TLAB top and end
      MMTkAllocatorOffsets alloc_offsets = get_tlab_top_and_end_offsets(selector);

      Node* thread = x->transform_later(new ThreadLocalNode());
      eden_top_adr = x->basic_plus_adr(x->top()/*not oop*/, thread, alloc_offsets.tlab_top_offset);
      eden_end_adr = x->basic_plus_adr(x->top()/*not oop*/, thread, alloc_offsets.tlab_end_offset);
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
    Node *old_eden_top;

    if (selector.tag == TAG_MARK_COMPACT) {
      Node *offset = ConLNode::make(extra_header);
      x->transform_later(offset);
      Node *node = new LoadPNode(ctrl, contended_phi_rawmem, eden_top_adr, TypeRawPtr::BOTTOM, TypeRawPtr::BOTTOM, MemNode::unordered);
      x->transform_later(node);
      old_eden_top = new AddPNode(x->top(), node, offset);
    } else {
      old_eden_top = new LoadPNode(ctrl, contended_phi_rawmem, eden_top_adr, TypeRawPtr::BOTTOM, TypeRawPtr::BOTTOM, MemNode::unordered);
    }
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

    bool enable_global_alloc_bit = false;
    #ifdef MMTK_ENABLE_GLOBAL_ALLOC_BIT
    enable_global_alloc_bit = true;
    #endif
  if (enable_global_alloc_bit || selector.tag == TAG_MARK_COMPACT) {
    // set the alloc bit:
    // intptr_t addr = (intptr_t) (void*) fast_oop;
    // uint8_t* meta_addr = (uint8_t*) (ALLOC_BIT_BASE_ADDRESS + (addr >> 6));
    // intptr_t shift = (addr >> 3) & 0b111;
    // uint8_t byte_val = *meta_addr;
    // uint8_t new_byte_val = byte_val | (1 << shift);
    // *meta_addr = new_byte_val;
    Node *obj_addr = new CastP2XNode(fast_oop_ctrl, fast_oop);
    x->transform_later(obj_addr);

    Node *addr_shift = ConINode::make(6);
    x->transform_later(addr_shift);

    Node *meta_offset = new URShiftLNode(obj_addr, addr_shift);
    x->transform_later(meta_offset);

    Node *meta_base = ConLNode::make(ALLOC_BIT_BASE_ADDRESS);
    x->transform_later(meta_base);

    Node *meta_addr = new AddLNode(meta_base, meta_offset);
    x->transform_later(meta_addr);

    Node *meta_addr_p = new CastX2PNode(meta_addr);
    x->transform_later(meta_addr_p);

    Node *meta_val = new LoadUBNode(fast_oop_ctrl, fast_oop_rawmem, meta_addr_p, TypePtr::BOTTOM, TypeInt::BYTE, MemNode::unordered);
    x->transform_later(meta_val);

    Node *meta_val_shift = ConINode::make(3);
    x->transform_later(meta_val_shift);

    Node *shifted_addr = new URShiftLNode(obj_addr, meta_val_shift);
    x->transform_later(shifted_addr);

    Node *meta_val_mask = ConLNode::make(0b111);
    x->transform_later(meta_val_mask);

    Node *shifted_masked_addr = new AndLNode(shifted_addr, meta_val_mask);
    x->transform_later(shifted_masked_addr);

    Node *const_one =  ConINode::make(1);
    x->transform_later(const_one);

    Node *shifted_masked_addr_i = new ConvL2INode(shifted_masked_addr);
    x->transform_later(shifted_masked_addr_i);

    Node *set_bit = new LShiftINode(const_one, shifted_masked_addr_i);
    x->transform_later(set_bit);

    Node *new_meta_val = new OrINode(meta_val, set_bit);
    x->transform_later(new_meta_val);

    Node *set_alloc_bit = new StoreBNode(fast_oop_ctrl, fast_oop_rawmem, meta_addr_p, TypeRawPtr::BOTTOM, new_meta_val, MemNode::unordered);
    x->transform_later(set_alloc_bit);

    fast_oop_rawmem = set_alloc_bit;
  }

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

bool MMTkBarrierSetC2::can_remove_barrier(GraphKit* kit, PhaseTransform* phase, Node* src, Node* slot, Node* val, bool skip_const_null) const {
  // Skip barrier if the new target is a null pointer.
  if (skip_const_null && val != NULL && val->is_Con() && val->bottom_type() == TypePtr::NULL_PTR) {
    return true;
  }
  // Barrier elision based on allocation node does not working well with slowpath-only allocation.
  if (!MMTK_ENABLE_ALLOCATION_FASTPATH) return false;
  // No barrier required for newly allocated objects.
  if (src == kit->just_allocated_object(kit->control())) return true;

  // Test if this store operation happens right after allocation.

  intptr_t      offset = 0;
  Node*         base   = AddPNode::Ideal_base_and_offset(slot, phase, offset);
  AllocateNode* alloc  = AllocateNode::Ideal_allocation(base, phase);

  if (offset == Type::OffsetBot) {
    return false; // cannot unalias unless there are precise offsets
  }

  if (alloc == NULL) {
     return false; // No allocation found
  }

  // Start search from Store node
  Node* mem = kit->control();
  if (mem->is_Proj() && mem->in(0)->is_Initialize()) {

    InitializeNode* st_init = mem->in(0)->as_Initialize();
    AllocateNode*  st_alloc = st_init->allocation();

    // Make sure we are looking at the same allocation
    if (alloc == st_alloc) {
      return true;
    }
  }

  return false;
}
