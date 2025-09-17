#ifndef MMTK_OPENJDK_MMTK_BARRIER_SET_C1_HPP
#define MMTK_OPENJDK_MMTK_BARRIER_SET_C1_HPP

#include "c1/c1_CodeStubs.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"

class MMTkBarrierSetAssembler;

class MMTkBarrierSetC1 : public BarrierSetC1 {
  friend class MMTkBarrierSetAssembler;

protected:
  CodeBlob* _pre_barrier_c1_runtime_code_blob;
  CodeBlob* _post_barrier_c1_runtime_code_blob;
  CodeBlob* _ref_load_barrier_c1_runtime_code_blob;

  /// Full pre-barrier
  virtual void object_reference_write_pre(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val, CodeEmitInfo* info) const {}
  /// Full post-barrier
  virtual void object_reference_write_post(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) const {}

  /// Substituting write barrier
  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value) override {
    if (access.is_oop()) object_reference_write_pre(access, access.base().opr(), access.resolved_addr(), value, access.patch_emit_info());
    BarrierSetC1::store_at_resolved(access, value);
    if (access.is_oop()) object_reference_write_post(access, access.base().opr(), access.resolved_addr(), value);
  }

  /// Substituting write barrier (cmpxchg)
  virtual LIR_Opr atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) override {
    if (access.is_oop()) object_reference_write_pre(access, access.base().opr(), access.resolved_addr(), new_value.result(), NULL);
    LIR_Opr result = BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
    if (access.is_oop()) object_reference_write_post(access, access.base().opr(), access.resolved_addr(), new_value.result());
    return result;
  }

  /// Substituting write barrier (xchg)
  virtual LIR_Opr atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) override {
    if (access.is_oop()) object_reference_write_pre(access, access.base().opr(), access.resolved_addr(), value.result(), NULL);
    LIR_Opr result = BarrierSetC1::atomic_xchg_at_resolved(access, value);
    if (access.is_oop()) object_reference_write_post(access, access.base().opr(), access.resolved_addr(), value.result());
    return result;
  }

  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register) override {
    return BarrierSetC1::resolve_address(access, resolve_in_register);
  }

  /// Helper function for C1 barrier implementations to resolve address in registers
  LIR_Opr resolve_address_in_register(LIRAccess& access, bool resolve_in_register) {
    DecoratorSet decorators = access.decorators();
    bool needs_patching = (decorators & C1_NEEDS_PATCHING) != 0;
    bool is_write = (decorators & C1_WRITE_ACCESS) != 0;
    bool is_array = (decorators & IS_ARRAY) != 0;
    bool on_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
    bool precise = is_array || on_anonymous;
    resolve_in_register |= !needs_patching && is_write && access.is_oop() && precise;
    return BarrierSetC1::resolve_address(access, resolve_in_register);
  }

public:

  MMTkBarrierSetC1()
    : _pre_barrier_c1_runtime_code_blob(NULL),
      _post_barrier_c1_runtime_code_blob(NULL) {}

  CodeBlob* pre_barrier_c1_runtime_code_blob() { return _pre_barrier_c1_runtime_code_blob; }
  CodeBlob* post_barrier_c1_runtime_code_blob() { return _post_barrier_c1_runtime_code_blob; }

  /// Generate C1 write barrier slow-call C1-LIR code
  virtual void generate_c1_runtime_stubs(BufferBlob* buffer_blob) override;
};

/// C1 pre write barrier slow-call stub.
/// The default behaviour is to call `MMTkBarrierSetRuntime::object_reference_write_pre_call` and pass all the three args.
/// Barrier implementations may inherit from this class, and override `emit_code` to perform a specialized slow-path call.
struct MMTkC1PreBarrierStub: CodeStub {
  LIR_Opr src, slot, new_val;
  CodeEmitInfo* info; // Code patching info
  LIR_PatchCode patch_code; // Enable code patching?
  LIR_Opr scratch = NULL; // Scratch register for the resolved field

  MMTkC1PreBarrierStub(LIR_Opr src, LIR_Opr slot, LIR_Opr new_val, CodeEmitInfo* info = NULL, LIR_PatchCode patch_code = lir_patch_none): src(src), slot(slot), new_val(new_val), info(info), patch_code(patch_code) {}

  virtual void emit_code(LIR_Assembler* ce) override;

  virtual void visit(LIR_OpVisitState* visitor) override {
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

  NOT_PRODUCT(virtual void print_name(outputStream* out) const { out->print("MMTkC1PreBarrierStub"); });
};

/// C1 post write barrier slow-call stub.
/// The default behaviour is to call `MMTkBarrierSetRuntime::object_reference_write_post_call` and pass all the three args.
/// Barrier implementations may inherit from this class, and override `emit_code` to perform a specialized slow-path call.
struct MMTkC1PostBarrierStub: CodeStub {
  LIR_Opr src, slot, new_val;

  MMTkC1PostBarrierStub(LIR_Opr src, LIR_Opr slot, LIR_Opr new_val): src(src), slot(slot), new_val(new_val) {}

  virtual void emit_code(LIR_Assembler* ce) override;

  virtual void visit(LIR_OpVisitState* visitor) override {
    visitor->do_slow_case();
    if (src != NULL) visitor->do_input(src);
    if (slot != NULL) visitor->do_input(slot);
    if (new_val != NULL) visitor->do_input(new_val);
  }

  NOT_PRODUCT(virtual void print_name(outputStream* out) const { out->print("MMTkC1PostBarrierStub"); });
};

struct MMTkC1ReferenceLoadBarrierStub: CodeStub {
  LIR_Opr val;
  CodeEmitInfo* info; // Code patching info

  MMTkC1ReferenceLoadBarrierStub(LIR_Opr val, CodeEmitInfo* info = NULL): val(val), info(info) {}

  virtual void emit_code(LIR_Assembler* ce) override;

  virtual void visit(LIR_OpVisitState* visitor) override {
    if (info != NULL) {
      visitor->do_slow_case(info);
    } else {
      visitor->do_slow_case();
    }
    if (val != NULL) visitor->do_input(val);
  }

  NOT_PRODUCT(virtual void print_name(outputStream* out) const { out->print("MMTkC1ReferenceLoadBarrierStub"); });
};

#endif // MMTK_OPENJDK_MMTK_BARRIER_SET_C1_HPP
