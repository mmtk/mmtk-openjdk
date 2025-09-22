#ifndef MMTK_OPENJDK_MMTK_BARRIER_SET_C1_HPP
#define MMTK_OPENJDK_MMTK_BARRIER_SET_C1_HPP

#include "c1/c1_CodeStubs.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"

class MMTkBarrierSetAssembler;

class MMTkBarrierSetC1 : public BarrierSetC1 {
private:
  // Code blobs for calling into runtime functions.
  // Here in MMTkBarrierSetC1,
  // we have one "runtime code blob" for every runtime function we call,
  // i.e. `MMTkBarrierSetRuntime::*_call`
  // There is no general rules that enfoce this in OpenJDK,
  // except that these code blobs are global and implemented in machine-specific assembly.
  // Our barrier slow paths are relatively simple, i.e. calling into MMTk-core.
  // So we only need such "runtime code blobs" for calling MMTk functions.
  // If we want to implement medium paths in machine-specific ways,
  // we may consider defining new code blobs for specific barriers.
  CodeBlob* _load_reference_c1_runtime_code_blob;
  CodeBlob* _object_reference_write_pre_c1_runtime_code_blob;
  CodeBlob* _object_reference_write_post_c1_runtime_code_blob;
  CodeBlob* _object_reference_write_slow_c1_runtime_code_blob;

protected:
  /// Full pre-barrier
  virtual void object_reference_write_pre(LIRAccess& access) const {}
  /// Full post-barrier
  virtual void object_reference_write_post(LIRAccess& access) const {}

  /// Substituting write barrier
  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value) override {
    if (access.is_oop()) object_reference_write_pre(access);
    BarrierSetC1::store_at_resolved(access, value);
    if (access.is_oop()) object_reference_write_post(access);
  }

  /// Substituting write barrier (cmpxchg)
  virtual LIR_Opr atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) override {
    if (access.is_oop()) object_reference_write_pre(access);
    LIR_Opr result = BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
    if (access.is_oop()) object_reference_write_post(access);
    return result;
  }

  /// Substituting write barrier (xchg)
  virtual LIR_Opr atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) override {
    if (access.is_oop()) object_reference_write_pre(access);
    LIR_Opr result = BarrierSetC1::atomic_xchg_at_resolved(access, value);
    if (access.is_oop()) object_reference_write_post(access);
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

  MMTkBarrierSetC1() {}

  CodeBlob* load_reference_c1_runtime_code_blob() { return _load_reference_c1_runtime_code_blob; }
  CodeBlob* object_reference_write_pre_c1_runtime_code_blob() { return _object_reference_write_pre_c1_runtime_code_blob; }
  CodeBlob* object_reference_write_post_c1_runtime_code_blob() { return _object_reference_write_post_c1_runtime_code_blob; }
  CodeBlob* object_reference_write_slow_c1_runtime_code_blob() { return _object_reference_write_slow_c1_runtime_code_blob; }

  /// Generate C1 write barrier slow-call C1-LIR code
  virtual void generate_c1_runtime_stubs(BufferBlob* buffer_blob) override;
};

/// The code stub for (weak) reference loading barrier slow path.
/// It will call `MMTkBarrierSetRuntime::load_reference_call` if `val` is not null.
/// Currently only the SATB barrier uses this code stub.
struct MMTkC1ReferenceLoadBarrierStub: CodeStub {
  LIR_Opr val;

  MMTkC1ReferenceLoadBarrierStub(LIR_Opr val): val(val) {}

  virtual void emit_code(LIR_Assembler* ce) override;

  virtual void visit(LIR_OpVisitState* visitor) override {
    visitor->do_slow_case();
    assert(val->is_valid(), "val must be valid");
    visitor->do_input(val);
  }

  NOT_PRODUCT(virtual void print_name(outputStream* out) const { out->print("MMTkC1ReferenceLoadBarrierStub"); });
};

#endif // MMTK_OPENJDK_MMTK_BARRIER_SET_C1_HPP
