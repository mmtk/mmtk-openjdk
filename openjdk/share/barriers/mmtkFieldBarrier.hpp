#ifndef MMTK_BARRIERS_FIELD_LOGGING_BARRIER
#define MMTK_BARRIERS_FIELD_LOGGING_BARRIER

#include "opto/callnode.hpp"
#include "opto/idealKit.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "gc/shared/barrierSet.hpp"
#include "../mmtk.h"
#include "../mmtkBarrierSet.hpp"
#include "../cpu/x86/mmtkBarrierSetAssembler_x86.hpp"
#include "../mmtkBarrierSetC1.hpp"
#include "../mmtkBarrierSetC2.hpp"

#define SIDE_METADATA_WORST_CASE_RATIO_LOG 1
#define LOG_BYTES_IN_CHUNK 22
#define CHUNK_MASK ((1L << LOG_BYTES_IN_CHUNK) - 1)

class MMTkFieldBarrierSetRuntime: public MMTkBarrierSetRuntime {
public:
  // Interfaces called by `MMTkBarrierSet::AccessBarrier`
  virtual void object_reference_write_pre(oop src, oop* slot, oop target) const override;
  virtual void object_reference_array_copy_pre(oop* src, oop* dst, size_t count) const override {
    if (FIELD_BARRIER_NO_ARRAYCOPY || count == 0) return;
    ::mmtk_array_copy_pre((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) src, (void*) dst, count);
  }
  virtual void object_probable_write(oop new_obj) const override;
  virtual void load_reference(DecoratorSet decorators, oop value) const override;
};

struct MMTkC1FieldBarrierStub: CodeStub {
  LIR_Opr src, slot, new_val;
  CodeEmitInfo* info; // Code patching info
  LIR_PatchCode patch_code; // Enable code patching?
  LIR_Opr scratch = NULL; // Scratch register for the resolved field

  MMTkC1FieldBarrierStub(LIR_Opr src, LIR_Opr slot, LIR_Opr new_val, CodeEmitInfo* info = NULL, LIR_PatchCode patch_code = lir_patch_none): src(src), slot(slot), new_val(new_val), info(info), patch_code(patch_code) {
    FrameMap* f = Compilation::current()->frame_map();
    f->update_reserved_argument_area_size(3 * BytesPerWord);
  }

  virtual void emit_code(LIR_Assembler* ce) override;

  virtual void visit(LIR_OpVisitState* visitor) override;

  NOT_PRODUCT(virtual void print_name(outputStream* out) const { out->print("MMTkC1FieldBarrierStub"); });
};

class MMTkFieldBarrierSetAssembler: public MMTkBarrierSetAssembler {
protected:
  virtual void object_reference_write_pre(MacroAssembler* masm, DecoratorSet decorators, Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) const override;
public:
  virtual void generate_c1_pre_write_barrier_stub(LIR_Assembler* ce, MMTkC1FieldBarrierStub* stub) const;
  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register src, Register dst, Register count) override;
  virtual void load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type, Register dst, Address src, Register tmp1, Register tmp_thread) override;
};

class MMTkFieldBarrierSetC1: public MMTkBarrierSetC1 {
protected:
  virtual void object_reference_write_pre(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const override;

  virtual void load_at_resolved(LIRAccess& access, LIR_Opr result) override;

  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register) override {
    return MMTkBarrierSetC1::resolve_address_in_register(access, resolve_in_register);
  }
};

class MMTkFieldBarrierSetC2: public MMTkBarrierSetC2 {
protected:
  virtual void object_reference_write_pre(GraphKit* kit, Node* src, Node* slot, Node* val) const override;
public:
  virtual bool array_copy_requires_gc_barriers(bool tightly_coupled_alloc, BasicType type, bool is_clone, bool is_clone_instance, ArrayCopyPhase phase) const override {
    return is_reference_type(type) && !tightly_coupled_alloc && !is_clone;
  }
  virtual Node* load_at_resolved(C2Access& access, const Type* val_type) const override;
};

struct MMTkFieldBarrier: MMTkBarrierImpl<
  MMTkFieldBarrierSetRuntime,
  MMTkFieldBarrierSetAssembler,
  MMTkFieldBarrierSetC1,
  MMTkFieldBarrierSetC2
> {};

#endif
