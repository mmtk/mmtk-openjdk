#include "precompiled.hpp"
#include "utilities/macros.hpp"

// The HybridBarrier is only implemented on x86_64 for now.
#ifdef AMD64

#include "mmtkHybridBarrier.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.inline.hpp"
#include "oops/objArrayOop.inline.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/interfaceSupport.inline.hpp"

//////////////////// Runtime ////////////////////

static inline MMTk_Mutator current_mutator() {
  return (MMTk_Mutator) &Thread::current()->third_party_heap_mutator;
}

void MMTkHybridBarrierSetRuntime::object_reference_array_copy_pre_src_only_call(void* src, void* dst, size_t count) {
  ::mmtk_array_copy_pre_src_only(current_mutator(), src, dst, count);
}

void MMTkHybridBarrierSetRuntime::object_clone_pre_call(void* src) {
  // Go's `bulkBarrierPreWriteSrcOnly`: the clone is a new (black) object, so its old contents need
  // no logging, but every reference copied into it must be shaded.  Shade the reference fields of
  // the source.  Instance fields are described by the oop map blocks, each a contiguous range of
  // slots.
  oop obj = cast_to_oop(src);
  Klass* k = obj->klass();
  MMTk_Mutator mutator = current_mutator();
  if (k->is_objArray_klass()) {
    objArrayOop array = objArrayOop(obj);
    int length = array->length();
    if (length > 0) {
      ::mmtk_array_copy_pre_src_only(mutator, (void*) array->base(), (void*) array->base(), (size_t) length);
    }
  } else if (k->is_instance_klass()) {
    InstanceKlass* ik = InstanceKlass::cast(k);
    OopMapBlock* map = ik->start_of_nonstatic_oop_maps();
    OopMapBlock* const end = map + ik->nonstatic_oop_map_count();
    for (; map < end; map++) {
      if (map->count() == 0) continue;
      void* start = (void*) obj->field_addr<char>(map->offset());
      ::mmtk_array_copy_pre_src_only(mutator, start, start, (size_t) map->count());
    }
  }
  // Type arrays have no reference fields.
}

void MMTkHybridBarrierSetRuntime::object_reference_write_pre(oop src, oop* slot, oop target) const {
  if (mmtk_enable_barrier_fastpath) {
    if (is_active()) {
      object_reference_write_slow_call((void*) src, (void*) slot, (void*) target);
    }
  } else {
    object_reference_write_pre_call((void*) src, (void*) slot, (void*) target);
  }
}

void MMTkHybridBarrierSetRuntime::object_reference_array_copy_pre(oop* src, oop* dst, size_t count) const {
  if (count == 0) return;
  if (!mmtk_enable_barrier_fastpath || is_active()) {
    object_reference_array_copy_pre_call((void*) src, (void*) dst, count);
  }
}

void MMTkHybridBarrierSetRuntime::object_reference_array_copy_pre_dst_uninitialized(oop* src, oop* dst, size_t count) const {
  if (count == 0) return;
  if (!mmtk_enable_barrier_fastpath || is_active()) {
    object_reference_array_copy_pre_src_only_call((void*) src, (void*) dst, count);
  }
}

void MMTkHybridBarrierSetRuntime::object_clone_pre(oop src, oop dst) const {
  if (!mmtk_enable_barrier_fastpath || is_active()) {
    object_clone_pre_call((void*) src);
  }
}

void MMTkHybridBarrierSetRuntime::object_probable_write(oop new_obj) const {
  // Intentionally blank, as for the SATBBarrier.  This is called after a C2 slow-path allocation.
  // C2 only elides the barrier of a store into a just-allocated object if it stores null (see
  // `MMTkHybridBarrierSetC2::object_reference_write_pre`), so the fields of `new_obj` hold nothing
  // that needs shading.
}

void MMTkHybridBarrierSetRuntime::object_reference_write_not_in_heap(oop value) const {
  // Stores into VM roots (OopStorage handles such as JNI global refs, and other off-heap oops).
  // VM roots are scanned once, when marking starts, so an object stored into one later must be
  // shaded, or it is lost if the (not yet scanned) stack that held it drops it.  Only the new value
  // needs shading: the root was scanned, so the old value is already marked.
  if (value != NULL && is_active()) {
    void* obj = (void*) value;
    ::mmtk_shade_objects(&obj, 1);
  }
}

void MMTkHybridBarrierSetRuntime::load_reference(DecoratorSet decorators, oop value) const {
  if (mmtk_enable_reference_load_barrier && value != NULL && is_active()) {
    ::mmtk_load_reference(current_mutator(), (void*) value);
  }
}

//////////////////// C1 ////////////////////

#ifdef COMPILER1

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkHybridBarrierSetC1::load_at_resolved(LIRAccess& access, LIR_Opr result) {
  weak_reference_load_at_resolved(access, result, uintptr_t(hybrid_barrier_active_address()));
}

void MMTkHybridBarrierSetC1::object_reference_write_pre(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const {
  LIRGenerator* gen = access.gen();
  DecoratorSet decorators = access.decorators();
  if ((decorators & IN_HEAP) == 0) return;
  bool needs_patching = (decorators & C1_NEEDS_PATCHING) != 0;

  // Make sure `src` is in a register (see `MMTkUnlogBitBarrierSetC1::emit_check_unlog_bit_fast_path`).
  LIR_Opr src_reg = gen->new_register(T_OBJECT);
  __ move(src, src_reg);

  // Resolve the slot address into a register, unless the field offset is not known yet.
  LIR_Opr slot_reg = LIR_OprFact::illegalOpr;
  if (!needs_patching) {
    if (slot->is_register()) {
      slot_reg = slot;
    } else {
      LIR_Address* address = slot->as_address_ptr();
      slot_reg = gen->new_pointer_register();
      if (!address->index()->is_valid() && address->disp() == 0) {
        __ move(address->base(), slot_reg);
      } else {
        assert(address->disp() != max_jint, "lea doesn't support patched addresses!");
        __ leal(slot, slot_reg);
      }
    }
  }

  // Make sure `new_val` is in a register.  It may be a constant (e.g. null).
  LIR_Opr new_val_reg = LIR_OprFact::illegalOpr;
  if (!needs_patching) {
    if (new_val->is_register()) {
      new_val_reg = new_val;
    } else {
      new_val_reg = gen->new_register(T_OBJECT);
      __ move(new_val, new_val_reg);
    }
  }

  MMTkC1HybridBarrierStub* slow = new MMTkC1HybridBarrierStub(src_reg, slot_reg, new_val_reg, needs_patching);

  if (mmtk_enable_barrier_fastpath) {
    // if (*hybrid_barrier_active_address() != 0) goto slow;
    LIR_Opr flag_addr_opr = gen->new_pointer_register();
    __ move(LIR_OprFact::longConst(uintptr_t(hybrid_barrier_active_address())), flag_addr_opr);
    LIR_Address* flag_addr = new LIR_Address(flag_addr_opr, T_BYTE);
    LIR_Opr flag = gen->new_register(T_INT);
    __ move(flag_addr, flag);
    __ cmp(lir_cond_notEqual, flag, LIR_OprFact::intConst(0));
    __ branch(lir_cond_notEqual, slow);
  } else {
    __ jump(slow);
  }

  __ branch_destination(slow->continuation());
}

#undef __

#endif // COMPILER1

//////////////////// C2 ////////////////////

#ifdef COMPILER2

#define __ ideal.

void MMTkHybridBarrierSetC2::object_reference_write_pre(GraphKit* kit, Node* src, Node* slot, Node* val) const {
  // A store into a just-allocated object can skip the barrier only if it stores null.  The old
  // value is null, so there is nothing to log for the deletion half, but a non-null new value must
  // still be logged (the insertion half), because the new object is allocated black and will not be
  // scanned.  Go's compiler elides barriers for new objects under the same condition (the value is
  // not a heap pointer).
  bool val_is_null = val != NULL && val->is_Con() && val->bottom_type() == TypePtr::NULL_PTR;
  if (val_is_null && can_remove_barrier(kit, &kit->gvn(), src, slot, val, /* skip_const_null */ false)) return;

  MMTkIdealKit ideal(kit, true);
  const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM);

  if (mmtk_enable_barrier_fastpath) {
    float unlikely = PROB_UNLIKELY(0.999);
    Node* zero = __ ConI(0);
    Node* flag = __ load(__ ctrl(), __ ConP((intptr_t) hybrid_barrier_active_address()), TypeInt::INT, T_BYTE, Compile::AliasIdxRaw);
    __ if_then(flag, BoolTest::ne, zero, unlikely); {
      // Unsafe stores may have a null base.  They are not heap stores, so skip the barrier.
      __ if_then(src, BoolTest::ne, kit->null()); {
        __ make_leaf_call(tf, FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_slow_call), "mmtk_barrier_call", src, slot, val);
      } __ end_if();
    } __ end_if();
  } else {
    __ make_leaf_call(tf, FN_ADDR(MMTkBarrierSetRuntime::object_reference_write_pre_call), "mmtk_barrier_call", src, slot, val);
  }

  kit->final_sync(ideal); // Final sync IdealKit and GraphKit.
}

Node* MMTkHybridBarrierSetC2::load_at_resolved(C2Access& access, const Type* val_type) const {
  return weak_reference_load_at_resolved(access, val_type, uintptr_t(hybrid_barrier_active_address()));
}

void MMTkHybridBarrierSetC2::clone(GraphKit* kit, Node* src, Node* dst, Node* size, bool is_array) const {
  // `BarrierSetC2::clone` copies the object as raw words, without barriers.  Shade the reference
  // fields of `src` before the copy (see `MMTkHybridBarrierSetRuntime::object_clone_pre_call`).
  {
    MMTkIdealKit ideal(kit, true);
    const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM);
    if (mmtk_enable_barrier_fastpath) {
      float unlikely = PROB_UNLIKELY(0.999);
      Node* zero = __ ConI(0);
      Node* flag = __ load(__ ctrl(), __ ConP((intptr_t) hybrid_barrier_active_address()), TypeInt::INT, T_BYTE, Compile::AliasIdxRaw);
      __ if_then(flag, BoolTest::ne, zero, unlikely); {
        __ make_leaf_call(tf, FN_ADDR(MMTkHybridBarrierSetRuntime::object_clone_pre_call), "mmtk_barrier_call", src);
      } __ end_if();
    } else {
      __ make_leaf_call(tf, FN_ADDR(MMTkHybridBarrierSetRuntime::object_clone_pre_call), "mmtk_barrier_call", src);
    }
    kit->final_sync(ideal);
  }
  MMTkBarrierSetC2::clone(kit, src, dst, size, is_array);
}

#undef __

#endif // COMPILER2

#endif // AMD64
