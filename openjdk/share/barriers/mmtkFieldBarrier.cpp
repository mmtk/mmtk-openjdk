#define private public // too lazy to change openjdk...
#define protected public
#include "mmtk.h"
#include "mmtkFieldBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"


#define SOFT_REFERENCE_LOAD_BARRIER true

constexpr int kUnloggedValue = 1;

static inline intptr_t side_metadata_base_address() {
  return UseCompressedOops ? FIELD_UNLOG_BITS_BASE_ADDRESS_COMPRESSED : FIELD_UNLOG_BITS_BASE_ADDRESS;
}


void MMTkFieldBarrierSetRuntime::load_reference(DecoratorSet decorators, oop value) const {
#if SOFT_REFERENCE_LOAD_BARRIER
  if (CONCURRENT_MARKING_ACTIVE == 1 && value != NULL && mmtk_get_rc((void*) value) != 0)
    ::mmtk_load_reference((void*) value, (MMTk_Mutator) &Thread::current()->third_party_heap_mutator);
#endif
};

void MMTkFieldBarrierSetRuntime::object_probable_write(oop new_obj) const {
  if (!RC_ENABLED || mmtk_get_rc((void*) new_obj) != 0) {
    ::mmtk_object_probable_write((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) new_obj);
  }
}

void MMTkFieldBarrierSetRuntime::object_reference_write_pre(oop src, oop* slot, oop target) const {
  if (mmtk_enable_barrier_fastpath) {
    intptr_t addr = ((intptr_t) (void*) slot);
    const volatile uint8_t * meta_addr = (const volatile uint8_t *) (side_metadata_base_address() + (addr >> (UseCompressedOops ? 5 : 6)));
    uint8_t byte_val = *meta_addr;
    if (!FIELD_BARRIER_NO_EAGER_BRANCH)
      if (byte_val == 0) return;
    intptr_t shift = (addr >> (UseCompressedOops ? 2 : 3)) & 0b111;
    if (((byte_val >> shift) & 1) == kUnloggedValue) {
      ::mmtk_object_reference_write_slow((void*) src, (void*) slot, (void*) target, (MMTk_Mutator) &Thread::current()->third_party_heap_mutator);
    }
  } else {
    ::mmtk_object_reference_write_pre((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) src, (void*) slot, (void*) target);
  }
}

#define __ masm->

void MMTkFieldBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp_thread) {
  bool on_oop = type == T_OBJECT || type == T_ARRAY;
  bool on_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool on_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool on_reference = on_weak || on_phantom;
  BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
#if SOFT_REFERENCE_LOAD_BARRIER
  if (on_oop && on_reference) {
    Label done;

    assert_different_registers(dst, tmp1);

    // No slow-call if SATB is not active
    // intptr_t tmp1_q = CONCURRENT_MARKING_ACTIVE;
    __ movptr(tmp1, intptr_t(&CONCURRENT_MARKING_ACTIVE));
    // Load with zero extension to 32 bits.
    // uint32_t tmp1_l = (uint32_t)(*(unt8_t*)tmp1_q);
    __ movzbl(tmp1, Address(tmp1, 0));
    // if (tmp1_l == 0) goto done;
    __ testl(tmp1, tmp1);
    __ jcc(Assembler::zero, done);
    // if (dst == 0) goto done;
    __ testptr(dst, dst);
    __ jcc(Assembler::zero, done);
    // Do slow-call
    __ push_call_clobbered_registers(false /* save_fpu */);
    __ mov(c_rarg0, dst);
    Address mutator(r15_thread, in_bytes(JavaThread::third_party_heap_mutator_offset()));
    __ lea(c_rarg1, mutator);
    __ MacroAssembler::call_VM_leaf_base(FN_ADDR(mmtk_load_reference), 2);
    __ pop_call_clobbered_registers(false /* save_fpu */);
    __ bind(done);
  }
#endif
}

void MMTkFieldBarrierSetAssembler::object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) const {
  if (can_remove_barrier(decorators, val, /* skip_const_null */ false)) return;
  if (mmtk_enable_barrier_fastpath) {
    Label done;

    assert_different_registers(tmp1, tmp2,  dst.base(),  dst.index());
    assert_different_registers(rcx, tmp2);

    // tmp2 = load-byte (side_metadata_base_address() + (obj >> 6));
    __ lea(tmp1, dst);
    __ shrptr(tmp1, UseCompressedOops ? 5 : 6);
    __ movptr(tmp2, side_metadata_base_address());
    __ movzbl(tmp2, Address(tmp2, tmp1));
    if (!FIELD_BARRIER_NO_EAGER_BRANCH) {
      __ cmpl(tmp2, 0);
      __ jcc(Assembler::equal, done);
    }
    // tmp1 = (obj >> 3) & 7
    __ lea(tmp1, dst);
    __ shrptr(tmp1, UseCompressedOops ? 2 : 3);
    __ andptr(tmp1, 7);
    // tmp2 = tmp2 >> tmp1
    __ xchgptr(tmp1, rcx);
    __ shrptr(tmp2);
    __ xchgptr(tmp1, rcx);
    // if ((tmp2 & 1) == 1) goto slowpath;
    __ andptr(tmp2, 1);
    __ cmpptr(tmp2, kUnloggedValue);
    __ jcc(Assembler::notEqual, done);

    // TODO: Spill fewer registers
    __ push_call_clobbered_registers(false /* save_fpu */);
    __ movptr(c_rarg0, dst.base());
    __ lea(c_rarg1, dst);
    if (val == noreg)
      __ movptr(c_rarg2, NULL_WORD);
    else
      __ movptr(c_rarg2, val);
    Address mutator(r15_thread, in_bytes(JavaThread::third_party_heap_mutator_offset()));
    __ lea(c_rarg3, mutator);
    __ call_VM_leaf_base(FN_ADDR(mmtk_object_reference_write_slow), 4);
    __ pop_call_clobbered_registers(false /* save_fpu */);

    __ bind(done);
  } else {
    __ pusha();
    __ movptr(c_rarg0, dst.base());
    __ lea(c_rarg1, dst);
    if (val == noreg)
      __ movptr(c_rarg2, NULL_WORD);
    else
      __ movptr(c_rarg2, val);
    __ call_VM_leaf_base(FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_pre_call), 3);
    __ popa();
  }
}

void MMTkFieldBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) {
  if (FIELD_BARRIER_NO_ARRAYCOPY) return;
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if (dest_uninitialized) return;
  if (type == T_OBJECT || type == T_ARRAY) {
    Label done;
    Address mutator(r15_thread, in_bytes(JavaThread::third_party_heap_mutator_offset()));
    // Bailout if count is zero
    __ cmpptr(count, 0);
    __ jcc(Assembler::equal, done);
    __ push_call_clobbered_registers(false /* save_fpu */);
    assert_different_registers(c_rarg0, dst, count);
    assert_different_registers(c_rarg1, count);
    if (c_rarg0 != src)   __ movptr(c_rarg0, src);
    if (c_rarg1 != dst)   __ movptr(c_rarg1, dst);
    if (c_rarg2 != count) __ movptr(c_rarg2, count);
    __ lea(c_rarg3, mutator);
    __ call_VM_leaf_base(FN_ADDR(mmtk_array_copy_pre), 4);
    __ pop_call_clobbered_registers(false /* save_fpu */);
    __ bind(done);
  }
}


#undef __
#define __ ce->masm()->

void MMTkFieldBarrierSetAssembler::generate_c1_pre_write_barrier_stub(LIR_Assembler* ce, MMTkC1FieldBarrierStub* stub) const {
  MMTkBarrierSetC1* bs = (MMTkBarrierSetC1*) BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());

  // For pre-barriers, stub->slot may not be a resolved address.
  // Manually patch the address and goes to the slow-path unconditionally.
  address runtime_address;
  if (stub->patch_code != lir_patch_none) {
    // Patch
    assert(stub->scratch->is_single_cpu(), "must be");
    assert(stub->scratch->is_register(), "Precondition.");
    ce->mem2reg(stub->slot, stub->scratch, T_OBJECT, stub->patch_code, stub->info, false /*wide*/);
    // Resolve address
    auto masm = ce->masm();
    LIR_Address* addr = stub->slot->as_address_ptr();
    Address from_addr = ce->as_Address(addr);
    __ lea(stub->scratch->as_register(), from_addr);
    // Store parameter
    ce->store_parameter(stub->scratch->as_pointer_register(), 1);
    runtime_address = bs->object_reference_write_pre_c1_runtime_code_blob_with_patch_fix()->code_begin();
  } else {
    // Store parameter
    ce->store_parameter(stub->slot->as_pointer_register(), 1);
    runtime_address = bs->object_reference_write_pre_c1_runtime_code_blob()->code_begin();
  }

  ce->store_parameter(stub->src->as_pointer_register(), 0);
  ce->store_parameter(stub->new_val->as_pointer_register(), 2);
  __ call(RuntimeAddress(runtime_address));
  __ jmp(*stub->continuation());
}

#undef __

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkFieldBarrierSetC1::load_at_resolved(LIRAccess& access, LIR_Opr result) {
  DecoratorSet decorators = access.decorators();
  bool is_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool is_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool is_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
  LIRGenerator *gen = access.gen();

  BarrierSetC1::load_at_resolved(access, result);

#if SOFT_REFERENCE_LOAD_BARRIER
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
#endif
}

void MMTkC1FieldBarrierStub::emit_code(LIR_Assembler* ce) {
    MMTkFieldBarrierSetAssembler* bs = (MMTkFieldBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
    bs->generate_c1_pre_write_barrier_stub(ce, this);
}

void MMTkC1FieldBarrierStub::visit(LIR_OpVisitState* visitor) {
    if (info != NULL) {
      visitor->do_slow_case(info);
    } else {
      visitor->do_slow_case();
    }
    if (src != NULL) visitor->do_input(src);
    if (slot != NULL) visitor->do_input(slot);
    if (new_val != NULL) visitor->do_input(new_val);
    if (scratch != NULL) {
      assert(scratch->is_oop(), "must be");
      visitor->do_temp(scratch);
    }
  }

void MMTkFieldBarrierSetC1::object_reference_write_pre(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const {
  LIRGenerator* gen = access.gen();
  DecoratorSet decorators = access.decorators();
  if ((decorators & IN_HEAP) == 0) return;
  bool needs_patching = (decorators & C1_NEEDS_PATCHING) != 0;
  if (!src->is_register()) {
    LIR_Opr reg = gen->new_pointer_register();
    if (src->is_constant()) {
      __ move(src, reg);
    } else {
      __ leal(src, reg);
    }
    src = reg;
  }
  assert(src->is_register(), "must be a register at this point");
  LabelObj *Ldone = new LabelObj();
  __ cmp(lir_cond_equal, src, LIR_OprFact::oopConst(NULL));
  __ branch(lir_cond_equal, Ldone->label());

  if (!slot->is_register() && !needs_patching) {
    LIR_Address* address = slot->as_address_ptr();
    LIR_Opr ptr = gen->new_pointer_register();
    if (!address->index()->is_valid() && address->disp() == 0) {
      __ move(address->base(), ptr);
    } else {
      assert(address->disp() != max_jint, "lea doesn't support patched addresses!");
      __ leal(slot, ptr);
    }
    slot = ptr;
  } else if (needs_patching && !slot->is_address()) {
    assert(slot->is_register(), "must be");
    slot = LIR_OprFact::address(new LIR_Address(slot, T_OBJECT));
  }
  assert(needs_patching || slot->is_register(), "must be a register at this point unless needs_patching");
  if (!new_val->is_register()) {
    LIR_Opr new_val_reg = gen->new_register(T_OBJECT);
    if (new_val->is_constant()) {
      __ move(new_val, new_val_reg);
    } else {
      __ leal(new_val, new_val_reg);
    }
    new_val = new_val_reg;
  }
  assert(new_val->is_register(), "must be a register at this point");
  MMTkC1FieldBarrierStub* slow = new MMTkC1FieldBarrierStub(src, slot, new_val, access.patch_emit_info(), needs_patching ? lir_patch_normal : lir_patch_none);
  if (needs_patching) slow->scratch = gen->new_register(T_OBJECT);

  if (mmtk_enable_barrier_fastpath) {
    if (needs_patching) {
      // FIXME: Jump to a medium-path for code patching without entering slow-path
      __ jump(slow);
    } else {
      LIR_Opr addr = slot;
      // uint8_t* meta_addr = (uint8_t*) (side_metadata_base_address() + (addr >> 6));
      LIR_Opr offset = gen->new_pointer_register();
      __ move(addr, offset);
      __ unsigned_shift_right(offset, UseCompressedOops ? 5 : 6, offset);
      LIR_Opr base = gen->new_pointer_register();
      __ move(LIR_OprFact::longConst(side_metadata_base_address()), base);
      LIR_Address* meta_addr = new LIR_Address(base, offset, T_BYTE);
      // uint8_t byte_val = *meta_addr;
      LIR_Opr byte_val = gen->new_register(T_INT);
      __ move(meta_addr, byte_val);
      // #if MMTK_BARRIER_EAGER_BRANCH
      // __ cmp(lir_cond_equal, byte_val, LIR_OprFact::intConst(0));
      // __ branch(lir_cond_equal, T_BYTE, Ldone->label());
      // #endif
      // intptr_t shift = (addr >> 3) & 0b111;
      LIR_Opr shift = gen->new_register(T_INT);
      __ move(addr, shift);
      __ unsigned_shift_right(shift, UseCompressedOops ? 2 : 3, shift);
      __ logical_and(shift, LIR_OprFact::intConst(0b111), shift);
      // if (((byte_val >> shift) & 1) == 1) slow;
      LIR_Opr result = byte_val;
      __ unsigned_shift_right(result, shift, result, LIR_OprFact::illegalOpr);
      __ logical_and(result, LIR_OprFact::intConst(1), result);
      __ cmp(lir_cond_equal, result, LIR_OprFact::intConst(1));
      __ branch(lir_cond_equal, slow);
    }
  } else {
    __ jump(slow);
  }

  __ branch_destination(slow->continuation());
  __ branch_destination(Ldone->label());
}

#undef __

#define __ ideal.


static void insert_write_barrier_common(MMTkIdealKit& ideal, Node* src, Node* slot, Node* val) {
  Node* no_base = __ top();
  Node* tls = __ thread();
  Node* mutator = __ AddP(no_base, tls, __ ConX(in_bytes(JavaThread::third_party_heap_mutator_offset())));
  if (mmtk_enable_barrier_fastpath) {
    float unlikely  = PROB_UNLIKELY(0.999);

    Node* zero  = __ ConI(0);
    Node* addr = __ CastPX(__ ctrl(), slot);
    Node* meta_addr = __ AddP(no_base, __ ConP(side_metadata_base_address()), __ URShiftX(addr, __ ConI(UseCompressedOops ? 5 : 6)));
    Node* byte = __ load(__ ctrl(), meta_addr, TypeInt::INT, T_BYTE, Compile::AliasIdxRaw);
    if (!FIELD_BARRIER_NO_EAGER_BRANCH)
      __ if_then(byte, BoolTest::ne, zero, unlikely);
    Node* shift = __ URShiftX(addr, __ ConI(UseCompressedOops ? 2 : 3));
    shift = __ AndI(__ ConvL2I(shift), __ ConI(7));
    Node* result = __ AndI(__ URShiftI(byte, shift), __ ConI(1));
    __ if_then(result, BoolTest::ne, zero, unlikely); {
      const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeRawPtr::NOTNULL);
      if (!FIELD_BARRIER_NO_C2_SLOW_CALL)
        Node* x = __ make_leaf_call(tf, FN_ADDR(mmtk_object_reference_write_slow), "mmtk_barrier_call", src, slot, val, mutator);
    } __ end_if();
    if (!FIELD_BARRIER_NO_EAGER_BRANCH)
      __ end_if();
  } else {
    const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM);
    Node* x = __ make_leaf_call(tf, FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_pre_call), "mmtk_barrier_call", src, slot, val);
    // Looks like this is necessary
    // See https://github.com/mmtk/openjdk/blob/c82e5c44adced4383162826c2c3933a83cfb139b/src/hotspot/share/gc/shenandoah/c2/shenandoahBarrierSetC2.cpp#L288-L291
    Node* call = __ ctrl()->in(0);
    call->add_req(slot);
  }
}

void MMTkFieldBarrierSetC2::object_reference_write_pre(GraphKit* kit, Node* src, Node* slot, Node* val) const {
  if (can_remove_barrier(kit, &kit->gvn(), src, slot, val, /* skip_const_null */ false)) return;

  MMTkIdealKit ideal(kit, true);

  insert_write_barrier_common(ideal, src, slot, val);

  kit->final_sync(ideal);
}

static void reference_load_barrier(GraphKit* kit, Node* slot, Node* val, bool emit_barrier) {
  MMTkIdealKit ideal(kit, true);
  Node* no_base = __ top();
  Node* tls = __ thread();
  Node* mutator = __ AddP(no_base, tls, __ ConX(in_bytes(JavaThread::third_party_heap_mutator_offset())));
  float unlikely  = PROB_UNLIKELY(0.999);
  Node* zero  = __ ConI(0);
  Node* cm_flag = __ load(__ ctrl(), __ ConP(uintptr_t(&CONCURRENT_MARKING_ACTIVE)), TypeInt::INT, T_BYTE, Compile::AliasIdxRaw);
  // No slow-call if SATB is not active
  __ if_then(cm_flag, BoolTest::ne, zero, unlikely); {
    // No slow-call if dst is NULL
    __ if_then(val, BoolTest::ne, kit->null()); {
      const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeRawPtr::NOTNULL);
      Node* x = __ make_leaf_call(tf, FN_ADDR(mmtk_load_reference), "mmtk_barrier_call", val, mutator);
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

Node* MMTkFieldBarrierSetC2::load_at_resolved(C2Access& access, const Type* val_type) const {
  DecoratorSet decorators = access.decorators();
  Node* adr = access.addr().node();
  Node* obj = access.base();

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

#if SOFT_REFERENCE_LOAD_BARRIER
  if (on_weak) {
    reference_load_barrier(kit, adr, load, true);
  } else if (unknown) {
    reference_load_barrier_for_unknown_load(kit, obj, offset, adr, load, !need_cpu_mem_bar);
  }
#endif

  return load;
}

#undef __