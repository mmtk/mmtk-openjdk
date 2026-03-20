#include "precompiled.hpp"
#include "mmtkSATBBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Runtime ////////////////////

void MMTkSATBBarrierSetRuntime::load_reference(DecoratorSet decorators, oop value) const {
  if (mmtk_enable_reference_load_barrier) {
    if (CONCURRENT_MARKING_ACTIVE == 1 && value != NULL)
      ::mmtk_load_reference((void*) value, (MMTk_Mutator) &Thread::current()->third_party_heap_mutator);
  }
};

void MMTkSATBBarrierSetRuntime::object_probable_write(oop new_obj) const {
  // We intentionally leave this method blank.
  // This method is called after slowpath allocation exits.
  // Because the new_obj is just allocated,
  // it does not have any fields holding old values for the SATB barrier to remember.
}

void MMTkSATBBarrierSetRuntime::object_reference_write_pre(oop src, oop* slot, oop target) const {
  if (mmtk_enable_barrier_fastpath) {
    if (is_unlog_bit_set(src)) {
      object_reference_write_slow_call((void*) src, (void*) slot, (void*) target);
    }
  } else {
    object_reference_write_pre_call((void*) src, (void*) slot, (void*) target);
  }
}

//////////////////// C1 ////////////////////

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkSATBBarrierSetC1::load_at_resolved(LIRAccess& access, LIR_Opr result) {
  DecoratorSet decorators = access.decorators();
  bool is_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool is_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool is_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
  LIRGenerator *gen = access.gen();

  BarrierSetC1::load_at_resolved(access, result);

  if (mmtk_enable_reference_load_barrier) {
    if (access.is_oop() && (is_weak || is_phantom || is_anonymous)) {
      // Register the value in the referent field with the pre-barrier
      LabelObj *Lcont_anonymous;
      if (is_anonymous) {
        Lcont_anonymous = new LabelObj();
        generate_referent_check(access, Lcont_anonymous);
      }
      assert(result->is_register(), "must be");
      assert(result->type() == T_OBJECT, "must be an object");
      auto slow = new MMTkC1ReferenceLoadBarrierStub(result);
      // Call slow-path only when concurrent marking is active
      LIR_Opr cm_flag_addr_opr = gen->new_pointer_register();
      __ move(LIR_OprFact::longConst(uintptr_t(&CONCURRENT_MARKING_ACTIVE)), cm_flag_addr_opr);
      LIR_Address* cm_flag_addr = new LIR_Address(cm_flag_addr_opr, T_BYTE);
      LIR_Opr cm_flag = gen->new_register(T_INT);
      __ move(cm_flag_addr, cm_flag);
      // No slow-call if SATB is not active
      __ cmp(lir_cond_equal, cm_flag, LIR_OprFact::intConst(1));
      __ branch(lir_cond_equal, slow);
      __ branch_destination(slow->continuation());
      if (is_anonymous) {
        __ branch_destination(Lcont_anonymous->label());
      }
    }
  }
}

void MMTkSATBBarrierSetC1::object_reference_write_pre(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const {
  object_reference_write_pre_or_post(access, src, /* pre = */ true);
}

#undef __

//////////////////// C2 ////////////////////

#define __ ideal.

void MMTkSATBBarrierSetC2::object_reference_write_pre(GraphKit* kit, Node* src, Node* slot, Node* val) const {
  if (can_remove_barrier(kit, &kit->gvn(), src, slot, val, /* skip_const_null */ false)) return;

  MMTkIdealKit ideal(kit, true);

  object_reference_write_pre_or_post(ideal, src, /* pre = */ true);

  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

static void reference_load_barrier(GraphKit* kit, Node* slot, Node* val, bool emit_barrier) {
  MMTkIdealKit ideal(kit, true);
  Node* no_base = __ top();
  float unlikely  = PROB_UNLIKELY(0.999);
  Node* zero  = __ ConI(0);
  Node* cm_flag = __ load(__ ctrl(), __ ConP(uintptr_t(&CONCURRENT_MARKING_ACTIVE)), TypeInt::INT, T_BYTE, Compile::AliasIdxRaw);
  // No slow-call if SATB is not active
  __ if_then(cm_flag, BoolTest::ne, zero, unlikely); {
    // No slow-call if dst is NULL
    __ if_then(val, BoolTest::ne, kit->null()); {
      const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM);
      Node* x = __ make_leaf_call(tf, FN_ADDR(MMTkBarrierSetRuntime::load_reference_call), "mmtk_barrier_call", val);
    } __ end_if();
  } __ end_if();
  kit->sync_kit(ideal);
  if (emit_barrier) kit->insert_mem_bar(Op_MemBarCPUOrder);
  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

static void reference_load_barrier_for_unknown_load(GraphKit* kit, Node* base_oop, Node* offset, Node* slot, Node* val, bool need_mem_bar) {
  // Note: This function is copied from G1BarrierSetC2::insert_pre_barrier,
  // and ShenandoahBarrierSetC2::insert_pre_barrier is probably copied from G1 as well.
  // It basically implements BarrierSetC1::generate_referent_check in C2 IR.
  // TODO: If another barrier needs weak reference load barrier,
  // consider hoisting this function to a superclass.

  // We could be accessing the referent field of a reference object. If so, when G1
  // is enabled, we need to log the value in the referent field in an SATB buffer.
  // This routine performs some compile time filters and generates suitable
  // runtime filters that guard the pre-barrier code.
  // Also add memory barrier for non volatile load from the referent field
  // to prevent commoning of loads across safepoint.

  // Some compile time checks.

  // If offset is a constant, is it java_lang_ref_Reference::_reference_offset?
  const TypeX* otype = offset->find_intptr_t_type();
  if (otype != nullptr && otype->is_con() &&
      otype->get_con() != java_lang_ref_Reference::referent_offset()) {
    // Constant offset but not the reference_offset so just return
    return;
  }

  // We only need to generate the runtime guards for instances.
  const TypeOopPtr* btype = base_oop->bottom_type()->isa_oopptr();
  if (btype != nullptr) {
    if (btype->isa_aryptr()) {
      // Array type so nothing to do
      return;
    }

    const TypeInstPtr* itype = btype->isa_instptr();
    if (itype != nullptr) {
      // Can the klass of base_oop be statically determined to be
      // _not_ a sub-class of Reference and _not_ Object?
      ciKlass* klass = itype->instance_klass();
      if (klass->is_loaded() &&
          !klass->is_subtype_of(kit->env()->Reference_klass()) &&
          !kit->env()->Object_klass()->is_subtype_of(klass)) {
        return;
      }
    }
  }

  // The compile time filters did not reject base_oop/offset so
  // we need to generate the following runtime filters
  //
  // if (offset == java_lang_ref_Reference::_reference_offset) {
  //   if (instance_of(base, java.lang.ref.Reference)) {
  //     pre_barrier(_, pre_val, ...);
  //   }
  // }

  float likely   = PROB_LIKELY(  0.999);
  float unlikely = PROB_UNLIKELY(0.999);

  IdealKit ideal(kit);

  Node* referent_off = __ ConX(java_lang_ref_Reference::referent_offset());

  __ if_then(offset, BoolTest::eq, referent_off, unlikely); {
    // Update graphKit memory and control from IdealKit.
    kit->sync_kit(ideal);

    Node* ref_klass_con = kit->makecon(TypeKlassPtr::make(kit->env()->Reference_klass()));
    Node* is_instof = kit->gen_instanceof(base_oop, ref_klass_con);

    // Update IdealKit memory and control from graphKit.
    __ sync_kit(kit);

    Node* one = __ ConI(1);
    // is_instof == 0 if base_oop == nullptr
    __ if_then(is_instof, BoolTest::eq, one, unlikely); {
      // Update graphKit from IdeakKit.
      kit->sync_kit(ideal);
      // Use the pre-barrier to record the value in the referent field
      reference_load_barrier(kit, slot, val, false);
      if (need_mem_bar) {
        // Add memory barrier to prevent commoning reads from this field
        // across safepoint since GC can change its value.
        kit->insert_mem_bar(Op_MemBarCPUOrder);
      }
      // Update IdealKit from graphKit.
      __ sync_kit(kit);
    } __ end_if(); // _ref_type != ref_none
  } __ end_if(); // offset == referent_offset

  // Final sync IdealKit and GraphKit.
  kit->final_sync(ideal);
}

Node* MMTkSATBBarrierSetC2::load_at_resolved(C2Access& access, const Type* val_type) const {
  // Mostly copied from G1.
  DecoratorSet decorators = access.decorators();
  Node* adr = access.addr().node();
  Node* obj = access.base();

  bool anonymous = (decorators & C2_UNSAFE_ACCESS) != 0;
  bool mismatched = (decorators & C2_MISMATCHED) != 0;
  bool unknown = (decorators & ON_UNKNOWN_OOP_REF) != 0;
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool in_native = (decorators & IN_NATIVE) != 0;
  bool on_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool on_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool is_unordered = (decorators & MO_UNORDERED) != 0;
  bool no_keepalive = (decorators & AS_NO_KEEPALIVE) != 0;
  bool is_mixed = !in_heap && !in_native;
  bool need_cpu_mem_bar = !is_unordered || mismatched || is_mixed;

  Node* top = Compile::current()->top();
  Node* offset = adr->is_AddP() ? adr->in(AddPNode::Offset) : top;

  // If we are reading the value of the referent field of a Reference
  // object (either by using Unsafe directly or through reflection)
  // then, if G1 is enabled, we need to record the referent in an
  // SATB log buffer using the pre-barrier mechanism.
  // Also we need to add memory barrier to prevent commoning reads
  // from this field across safepoint since GC can change its value.
  bool need_read_barrier = (((on_weak || on_phantom) && !no_keepalive) ||
                            (in_heap && unknown && offset != top && obj != top));

  if (!access.is_oop() || !need_read_barrier) {
    return BarrierSetC2::load_at_resolved(access, val_type);
  }

  // The other access "opt_access" is only used in arraycopy barriers.
  // OpenJDK doesn't have weak arrays, so it must be "parse_access".
  assert(access.is_parse_access(), "entry not supported at optimization time");

  C2ParseAccess& parse_access = static_cast<C2ParseAccess&>(access);
  GraphKit* kit = parse_access.kit();
  Node* load = BarrierSetC2::load_at_resolved(access, val_type);

  if (mmtk_enable_reference_load_barrier) {
    if (on_weak) {
      reference_load_barrier(kit, adr, load, true);
    } else if (unknown) {
      reference_load_barrier_for_unknown_load(kit, obj, offset, adr, load, !need_cpu_mem_bar);
    }
  }

  return load;
}

#undef __
