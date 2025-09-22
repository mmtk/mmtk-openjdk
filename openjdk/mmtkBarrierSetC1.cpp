#include "precompiled.hpp"
#include "c1/c1_CodeStubs.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"
#include "mmtkBarrierSetAssembler_x86.hpp"
#include "mmtkBarrierSetC1.hpp"

void MMTkBarrierSetC1::generate_c1_runtime_stubs(BufferBlob* buffer_blob) {
  using GenStubFunc = void(*)(StubAssembler*);
  class RuntimeCodeBlobCodeGenClosure : public StubAssemblerCodeGenClosure {
    GenStubFunc gen_stub;
  public:
    RuntimeCodeBlobCodeGenClosure(GenStubFunc gen_stub): gen_stub(gen_stub) {}

    virtual OopMapSet* generate_code(StubAssembler* sasm) override {
      gen_stub(sasm);
      return nullptr;
    }
  };

  auto do_code_blob = [buffer_blob](const char* name, GenStubFunc gen_stub) {
    RuntimeCodeBlobCodeGenClosure closure(gen_stub);
    return Runtime1::generate_blob(buffer_blob, -1, name, false, &closure);
  };

  _load_reference_c1_runtime_code_blob              = do_code_blob("load_reference",              &MMTkBarrierSetAssembler::generate_c1_load_reference_runtime_stub);
  _object_reference_write_pre_c1_runtime_code_blob  = do_code_blob("object_reference_write_pre",  &MMTkBarrierSetAssembler::generate_c1_object_reference_write_pre_runtime_stub);
  _object_reference_write_post_c1_runtime_code_blob = do_code_blob("object_reference_write_post", &MMTkBarrierSetAssembler::generate_c1_object_reference_write_post_runtime_stub);
  _object_reference_write_slow_c1_runtime_code_blob = do_code_blob("object_reference_write_slow", &MMTkBarrierSetAssembler::generate_c1_object_reference_write_slow_runtime_stub);
}

void MMTkC1ReferenceLoadBarrierStub::emit_code(LIR_Assembler* ce) {
  MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->generate_c1_ref_load_barrier_stub_call(ce, this);
}
